// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Guest threads on host pthreads. Each guest thread owns a FreeBSD-style TCB
 * (variant II: static TLS below the TCB). The loader rewrites the eboot's
 * `mov rax, fs:[0]` into `mov rax, gs:[0]`, so GS base = guest TCB while glibc
 * keeps FS. Priorities/affinity are recorded, not enforced by a PS4 scheduler. */
#define _GNU_SOURCE
#include "runtime.h"
#include "../gpu/shim/bb_env.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#ifndef _WIN32
#include <asm/prctl.h>
#endif
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define ATTR_MAGIC UINT32_C(0x41545452)
#define STACK_MARGIN (256*1024)
#define MIN_STACK (64*1024)
#define DEFAULT_STACK (1024*1024)
#define DEFAULT_PRIO 700

typedef void *(ABI *GuestEntry)(void *);
typedef struct ThreadAttr {
    uint32_t magic;
    int detached, policy, prio, inherit;
    uint64_t stack, guard, affinity;
    uint64_t stack_addr; /* kr-port: stack the game offered (scePthreadAttrSetstack); not used, see create() */
    struct ThreadAttr *next;
} ThreadAttr;
typedef struct GuestThread {
    /* Guest-visible TCB is allocated separately; this is the ScePthread handle. */
    uint64_t *tcb;
    unsigned char *tls_block;
    pthread_t host;
    GuestEntry entry;
    void *argument, *result;
    ThreadAttr attr;
    char name[32];
    int detached, finished, joined, host_owned;
    jmp_buf exit_jump;
    struct GuestThread *next;
    unsigned char *stack_memory; /* the low stack it runs on (Windows: switched to at its start) */
    size_t stack_bytes;
    int tid;   /* kr-port: host thread id (KR_STALL_PROFILE) */
} GuestThread;

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static ThreadAttr *attributes;
static GuestThread *threads;
static _Thread_local GuestThread *current;
static _Thread_local int32_t guest_errno;
static const unsigned char *tls_template;
static uint64_t tls_filesz, tls_memsz, tls_align=16;
static size_t created, joined_count, exited;

void runtime_set_main_tls(const void *data,uint64_t filesz,uint64_t memsz,uint64_t align) {
    tls_template=data; tls_filesz=filesz; tls_memsz=memsz; tls_align=align ? align : 16;
}
static uint64_t tls_offset(void) { return (tls_memsz+tls_align-1)&~(tls_align-1); }
static void set_gs(void *base) {
#ifdef _WIN32
    bb_guest_tls_set(base); /* GS is the TEB: the game reads its TLS slot (bbport_posix_c.h) */
#else
    if (syscall(SYS_arch_prctl,ARCH_SET_GS,(unsigned long)base)) { perror("STOP: arch_prctl(ARCH_SET_GS)"); exit(21); }
#endif
}
/* Build TCB/static TLS for the calling host thread and point GS at it. */
static void attach(GuestThread *t) {
    t->tid=(int)syscall(SYS_gettid);
    uint64_t offset=tls_offset();
    size_t total=offset+256;
    unsigned char *block=aligned_alloc(64,(total+63)&~(size_t)63);
    if (!block) { fputs("Cannot allocate guest TLS\n",stderr); exit(1); }
    memset(block,0,total);
    if (tls_filesz) memcpy(block,tls_template,tls_filesz);
    uint64_t *tcb=(uint64_t *)(block+offset);
    static uint64_t dtv[3];
    tcb[0]=(uint64_t)(uintptr_t)tcb;         /* tcb_self */
    tcb[1]=(uint64_t)(uintptr_t)dtv;         /* tcb_dtv (static module only) */
    tcb[2]=(uint64_t)(uintptr_t)t;           /* tcb_thread */
    t->tls_block=block; t->tcb=tcb;
    set_gs(tcb);
    current=t;
}
/* A thread's record is its ScePthread handle, and the game packs handles into 40-bit fields: the
 * owner of Bloodborne's hybrid lock (guest 0x20368c0) is masked to 0xffffffffff and then compared
 * with the whole handle, so a handle above 40 bits never matches its own lock and the thread sleeps
 * on its semaphore forever (lamp travel hung at loading: issue/PR #193). Records come from low
 * memory (below 40 bits, zeroed), carved out of 256 KiB pieces; they are never freed. */
static GuestThread *low_record(void) {
    static pthread_mutex_t pool_lock=PTHREAD_MUTEX_INITIALIZER;
    static unsigned char *pool;
    static size_t left;
    const size_t size=(sizeof(GuestThread)+63)&~(size_t)63, piece=256*1024;
    void *record=NULL;
    pthread_mutex_lock(&pool_lock);
    if (left<size) {
        pool=runtime_low_map(piece,PROT_READ|PROT_WRITE);
        left=pool ? piece : 0;
    }
    if (left>=size) { record=pool; pool+=size; left-=size; }
    pthread_mutex_unlock(&pool_lock);
    return record;
}
static GuestThread *new_thread(void) {
    GuestThread *t=low_record();
    if (!t) {
        fputs("Runtime: no low memory for a thread record; its handle may not fit 40 bits\n",stderr);
        t=calloc(1,sizeof(*t));
    }
    if (!t) return NULL;
    t->attr=(ThreadAttr){.magic=ATTR_MAGIC,.policy=1,.prio=DEFAULT_PRIO,.stack=DEFAULT_STACK,.affinity=0x7f};
    return t;
}
static void publish(GuestThread *t) {
    pthread_mutex_lock(&lock); t->next=threads; threads=t; pthread_mutex_unlock(&lock);
}
/* Host threads that call into guest code without being created by the guest
 * (the main thread, test threads) get a record on first use. */
GuestThread *runtime_thread_current(void) {
    if (current) return current;
    GuestThread *t=new_thread();
    if (!t) { fputs("Cannot allocate guest thread\n",stderr); exit(1); }
    t->host=pthread_self(); t->host_owned=1;
    snprintf(t->name,sizeof(t->name),"host");
    attach(t); publish(t);
    return t;
}
/* Host threads that call guest code (HLE decoders invoking guest callbacks) need
 * a TCB/TLS like guest threads; the record lives until process exit. */
void runtime_thread_attach_host(const char *name) {
    GuestThread *t=runtime_thread_current();
    snprintf(t->name,sizeof(t->name),"%s",name ? name : "host");
}
void runtime_re_watch_now(uint64_t address);
void runtime_thread_attach_main(void) {
    GuestThread *t=runtime_thread_current();
    snprintf(t->name,sizeof(t->name),"main");
    /* BB_TLS_WATCH=<offset from the thread pointer> (reverse engineering): who writes that static
     * TLS variable of the main thread (reported when the game faults). */
    const char *watch=bb_env("BB_TLS_WATCH");
    if (watch && *watch) runtime_re_watch_now((uint64_t)(uintptr_t)((unsigned char *)t->tcb+strtoll(watch,NULL,0)));
}
static GuestThread *find_thread(void *handle) {
    GuestThread *found=NULL;
    pthread_mutex_lock(&lock);
    for (GuestThread *t=threads;t;t=t->next) if (t==handle) { found=t; break; }
    pthread_mutex_unlock(&lock);
    return found;
}
static ThreadAttr *find_attr(ThreadAttr **slot) {
    if (!slot || !*slot) return NULL;
    ThreadAttr *found=NULL;
    pthread_mutex_lock(&lock);
    for (ThreadAttr *a=attributes;a;a=a->next) if (a==*slot && a->magic==ATTR_MAGIC) { found=a; break; }
    pthread_mutex_unlock(&lock);
    return found;
}

static ABI void *thread_self(void) { return runtime_thread_current(); }
static ABI int32_t *guest_error(void) { return &guest_errno; }
int32_t *runtime_errno(void) { return &guest_errno; }

static ABI int32_t attr_init(ThreadAttr **out) {
    if (!out) return ERR(22);
    ThreadAttr *a=calloc(1,sizeof(*a));
    if (!a) return ERR(12);
    *a=(ThreadAttr){.magic=ATTR_MAGIC,.policy=1,.prio=DEFAULT_PRIO,.inherit=4,.stack=DEFAULT_STACK,.guard=4096,.affinity=0x7f};
    pthread_mutex_lock(&lock); a->next=attributes; attributes=a; pthread_mutex_unlock(&lock);
    *out=a; return 0;
}
static ABI int32_t attr_destroy(ThreadAttr **slot) {
    ThreadAttr *a=find_attr(slot);
    if (!a) return ERR(22);
    pthread_mutex_lock(&lock);
    ThreadAttr **link=&attributes;
    while (*link!=a) link=&(*link)->next;
    *link=a->next; a->magic=0;
    pthread_mutex_unlock(&lock);
    free(a); *slot=NULL; return 0;
}
static ABI int32_t attr_get(void *thread,ThreadAttr **out) {
    ThreadAttr *a=find_attr(out);
    if (!a || !thread) return ERR(22);
    GuestThread *t=find_thread(thread);
    if (!t) return ERR(3);
    ThreadAttr *next=a->next;
    *a=t->attr; a->magic=ATTR_MAGIC; a->next=next;
    a->detached=t->detached;
    return 0;
}
static ABI int32_t attr_set_stack(ThreadAttr **slot,uint64_t size) {
    ThreadAttr *a=find_attr(slot);
    if (!a || size<16384) return ERR(22);
    a->stack=size; return 0;
}
static ABI int32_t attr_get_stack(ThreadAttr **slot,uint64_t *size) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !size) return ERR(22);
    *size=a->stack; return 0;
}
/* kr-port: scePthreadAttrSetstack(attr, addr, size). The thread runs on a stack of this size
 * that the runtime maps itself (below 1 TiB, as in create()), not on the game's memory: glibc
 * would put its thread descriptor and static TLS at the top of a caller-supplied stack, inside
 * memory the game owns. The address is kept for diagnostics. */
static ABI int32_t attr_set_stack_region(ThreadAttr **slot,void *addr,uint64_t size) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !addr || size<16384) return ERR(22);
    a->stack=size; a->stack_addr=(uint64_t)(uintptr_t)addr; return 0;
}
/* kr-port: condition variable attributes. cond_init (runtime_mutex.c) ignores them, so the
 * object only has to be a valid, distinct handle. */
static ABI int32_t condattr_init(void **attr) {
    if (!attr) return ERR(22);
    *attr=calloc(1,16);
    return *attr ? 0 : ERR(12);
}
static ABI int32_t condattr_destroy(void **attr) {
    if (!attr || !*attr) return ERR(22);
    free(*attr); *attr=NULL; return 0;
}
static ABI int32_t attr_set_detach(ThreadAttr **slot,int state) {
    ThreadAttr *a=find_attr(slot);
    if (!a || (state!=0 && state!=1)) return ERR(22);
    a->detached=state; return 0;
}
static ABI int32_t attr_get_detach(ThreadAttr **slot,int *state) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !state) return ERR(22);
    *state=a->detached; return 0;
}
static ABI int32_t attr_set_policy(ThreadAttr **slot,int policy) {
    ThreadAttr *a=find_attr(slot);
    if (!a || policy<1 || policy>3) return ERR(22);
    a->policy=policy; return 0;
}
static ABI int32_t attr_set_inherit(ThreadAttr **slot,int inherit) {
    ThreadAttr *a=find_attr(slot);
    if (!a || (inherit!=0 && inherit!=4)) return ERR(22);
    a->inherit=inherit; return 0;
}
static ABI int32_t attr_set_param(ThreadAttr **slot,const int *param) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !param) return ERR(22);
    a->prio=*param; return 0;
}
static ABI int32_t attr_get_param(ThreadAttr **slot,int *param) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !param) return ERR(22);
    *param=a->prio; return 0;
}
static ABI int32_t attr_set_affinity(ThreadAttr **slot,uint64_t mask) {
    ThreadAttr *a=find_attr(slot);
    if (!a) return ERR(22);
    a->affinity=mask; return 0;
}
static ABI int32_t attr_affinity(ThreadAttr **slot,uint64_t *mask) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !mask) return ERR(22);
    *mask=a->affinity; return 0;
}
static ABI int32_t attr_set_guard(ThreadAttr **slot,uint64_t size) {
    ThreadAttr *a=find_attr(slot);
    if (!a) return ERR(22);
    a->guard=size; return 0;
}

/* Linux thread names hold 15 characters; longer ones would be rejected. */
static void set_host_name(const char *name) {
    char host[16]={0};
    memcpy(host,name,strnlen(name,sizeof(host)-1));
    pthread_setname_np(pthread_self(),host);
#ifdef _WIN32
    bb_win32_set_thread_name(host);
#endif
}
static void run_guest(void *p) {
    GuestThread *t=p;
    if (!setjmp(t->exit_jump)) t->result=t->entry(t->argument);
}
void runtime_re_watch_thread(void);
static void stack_release(GuestThread *t, uint64_t delay_ns);   /* kr-port */
static void *host_start(void *p) {
    GuestThread *t=p;
    attach(t);
    runtime_re_watch_thread();
    set_host_name(t->name);
#ifdef _WIN32
    /* winpthreads ignores a given stack: the guest runs on its low one, fully committed (the
     * game's frames skip Windows' guard pages). */
    if (t->stack_memory) bb_run_on_stack(run_guest,t,t->stack_memory,t->stack_memory+t->stack_bytes);
    else run_guest(t);
#else
    run_guest(t);
#endif
    runtime_thread_keys_cleanup();
    pthread_mutex_lock(&lock); t->finished=1; ++exited; pthread_mutex_unlock(&lock);
    if (t->detached) stack_release(t,2000000000u);   /* kr-port: reused after the thread is gone */
    return t->result;
}
/* kr-port: KR_STALL_PROFILE=1. Each frame spike seen at 60 FPS followed the game creating a
 * thread; this samples the creating thread for 200 ms after every creation (every 1 ms, a signal:
 * RIP and the eboot return addresses found in the first 16 KiB of its stack) and prints the most
 * frequent eboot addresses, which point at what the game was doing meanwhile. */
#include <dirent.h>
#include <sys/prctl.h>
#include <stdint.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/uio.h>
static uintptr_t prof_base; static uint64_t prof_size;
static volatile pid_t prof_tid; static volatile uint64_t prof_until;
static char prof_label[64];
#define PROF_SLOTS 65536
#define PROF_DEPTH 6
static uint64_t prof_pc[PROF_SLOTS][PROF_DEPTH]; static int prof_who[PROF_SLOTS]; static volatile unsigned prof_count;
extern uintptr_t kr_stream_loop_return, kr_flip_loop_return;   /* kr-port: loops whose yield waits (thread_yield) */
void runtime_thread_profile_image(uintptr_t base, uint64_t size) { prof_base=base; prof_size=size; kr_stream_loop_return=base+0x2d74ce; kr_flip_loop_return=base+0x62d655; }
static uint64_t prof_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
static void prof_signal(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info;
    unsigned slot=__atomic_fetch_add(&prof_count,1,__ATOMIC_RELAXED);
    if (slot>=PROF_SLOTS) return;
    ucontext_t *uc=context;
    prof_who[slot]=(int)syscall(SYS_gettid);
    uint64_t *out=prof_pc[slot]; int n=0;
    uintptr_t rip=(uintptr_t)uc->uc_mcontext.gregs[REG_RIP], sp=(uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    out[n++]=rip-prof_base<prof_size ? rip-prof_base : 0;
    uint64_t words[512];
    for (int chunk=0; chunk<4 && n<PROF_DEPTH; ++chunk) {
        struct iovec local={words,sizeof(words)}, remote={(void *)(sp+(uintptr_t)chunk*sizeof(words)),sizeof(words)};
        if (process_vm_readv(getpid(),&local,1,&remote,1,0)!=(ssize_t)sizeof(words)) break;
        for (int i=0;i<512 && n<PROF_DEPTH;++i) if (words[i]-prof_base<prof_size && words[i]-prof_base>0x1000) out[n++]=words[i]-prof_base;
    }
    while (n<PROF_DEPTH) out[n++]=0;
}
static int prof_cmp(const void *a, const void *b) {
    const uint64_t *x=a, *y=b; return x[1]<y[1] ? 1 : x[1]>y[1] ? -1 : 0;
}
static void prof_report_thread(int tid, const char *name, unsigned total);
static void prof_report(void) {
    unsigned n=prof_count<PROF_SLOTS ? prof_count : PROF_SLOTS;
    if (!n) return;
    /* every sampled thread: its most frequent eboot return addresses (depth 1) */
    int tids[256]; unsigned nt=0;
    for (unsigned i=0;i<n;++i) {
        unsigned k; for (k=0;k<nt;++k) if (tids[k]==prof_who[i]) break;
        if (k==nt && nt<256) tids[nt++]=prof_who[i];
    }
    for (unsigned k=0;k<nt;++k) {
        char name[32]="?";
        pthread_mutex_lock(&lock);
        for (GuestThread *t=threads;t;t=t->next) if (t->tid==tids[k]) { snprintf(name,sizeof(name),"%s",t->name); break; }
        pthread_mutex_unlock(&lock);
        prof_report_thread(tids[k],name,n);
    }
    return;
    /* the most frequent addresses at each depth 0..2 */
    for (int depth=0; depth<3; ++depth) {
        static uint64_t table[PROF_SLOTS][2]; unsigned used=0;
        for (unsigned i=0;i<n;++i) {
            uint64_t pc=prof_pc[i][depth]; unsigned k;
            for (k=0;k<used;++k) if (table[k][0]==pc) { ++table[k][1]; break; }
            if (k==used && used<PROF_SLOTS) { table[used][0]=pc; table[used][1]=1; ++used; }
        }
        qsort(table,used,sizeof(table[0]),prof_cmp);
        printf("Stall profile (%s, %u samples) depth %d:",prof_label,n,depth);
        for (unsigned k=0;k<used && k<8;++k) printf(" %#llx x%llu",(unsigned long long)table[k][0],(unsigned long long)table[k][1]);
        printf("\n");
    }
    fflush(stdout);
}
static void prof_report_thread(int tid, const char *name, unsigned total) {
    (void)total;
    static uint64_t table[PROF_SLOTS][2]; unsigned used=0, samples=0, busy=0;
    for (unsigned i=0;i<PROF_SLOTS && i<prof_count;++i) {
        if (prof_who[i]!=tid) continue;
        ++samples; if (prof_pc[i][0]) ++busy;   /* in eboot code, not in a runtime call */
        uint64_t pc=prof_pc[i][0] ? prof_pc[i][0] : prof_pc[i][1]; unsigned k;
        for (k=0;k<used;++k) if (table[k][0]==pc) { ++table[k][1]; break; }
        if (k==used && used<PROF_SLOTS) { table[used][0]=pc; table[used][1]=1; ++used; }
    }
    qsort(table,used,sizeof(table[0]),prof_cmp);
    printf("Stall profile (%s) thread '%s': %u samples, %u in game code:",prof_label,name,samples,busy);
    for (unsigned k=0;k<used && k<5;++k) printf(" %#llx x%llu",(unsigned long long)table[k][0],(unsigned long long)table[k][1]);
    printf("\n");
}
static void *prof_thread(void *unused) {
    (void)unused;
    int active=0;
    for (;;) {
        uint64_t now=prof_now();
        if (now<prof_until && prof_tid) {
            active=1;
            pthread_mutex_lock(&lock);
            for (GuestThread *t=threads;t;t=t->next)
                if (t->tid && !t->finished) syscall(SYS_tgkill,getpid(),t->tid,SIGRTMIN+5);
            pthread_mutex_unlock(&lock);
            syscall(SYS_tgkill,getpid(),prof_tid,SIGRTMIN+5);
        } else if (active) {
            active=0; prof_report(); prof_count=0;
        }
        struct timespec t={0,1000000}; nanosleep(&t,NULL);
    }
    return NULL;
}
static void prof_mark(const char *name) {
    static int enabled=-1;
    if (enabled<0) {
        const char *e=getenv("KR_STALL_PROFILE"); enabled=e && *e=='1';
        if (enabled) {
            struct sigaction sa={0}; sa.sa_sigaction=prof_signal; sa.sa_flags=SA_SIGINFO|SA_RESTART;
            sigemptyset(&sa.sa_mask); sigaction(SIGRTMIN+5,&sa,NULL);
            pthread_t t; pthread_create(&t,NULL,prof_thread,NULL); pthread_detach(t);
        }
    }
    if (!enabled || prof_now()<prof_until) return;
    snprintf(prof_label,sizeof(prof_label),"after creating '%s'",name);
    prof_tid=(pid_t)syscall(SYS_gettid);
    prof_until=prof_now()+200000000u;
}
/* kr-port: KR_GAME_PROFILE=<file>. A continuous sampler of the game's logic threads (names
 * starting with an entry of KR_GAME_PROFILE_THREADS, default "main,runTaskMain,TaskJobThread"):
 * every KR_GAME_PROFILE_US (1000) each is sent a signal whose handler records the time, the thread,
 * its RIP, whether it sits on a syscall instruction (blocked in the kernel) and the first eboot
 * return addresses on its stack. Records go to the file as text, one per line:
 *   <ms> <tid> <rip: eboot offset, or h<host address>> <s if in a syscall> <eboot return addresses>
 * and a "T <tid> <name>" line per thread, for offline analysis (tools/game_profile.py). */
#define GP_SLOTS 65536
#define GP_DEPTH 4
#define GP_HOST 6
typedef struct { uint64_t ms_x1000; int tid; int in_syscall; uint64_t rip; uint64_t ret[GP_DEPTH]; uint64_t host[GP_HOST]; } GpSample;
/* The port's code (executable mappings of bb-probe and libbbgpu.so): return addresses into it. */
static struct { uintptr_t lo, hi; } gp_code[8]; static int gp_ncode;
static int gp_is_port(uint64_t a) { for (int i=0;i<gp_ncode;++i) if (a>=gp_code[i].lo && a<gp_code[i].hi) return 1; return 0; }
static GpSample gp_ring[GP_SLOTS];
static volatile unsigned gp_head, gp_tail;
static uint64_t gp_start;
static void gp_signal(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info;
    unsigned slot=__atomic_fetch_add(&gp_head,1,__ATOMIC_RELAXED);
    if (slot-gp_tail>=GP_SLOTS) return;   /* the writer fell behind: dropped */
    GpSample *s=&gp_ring[slot%GP_SLOTS];
    ucontext_t *uc=context;
    s->ms_x1000=(prof_now()-gp_start)/1000u;
    s->tid=(int)syscall(SYS_gettid);
    uintptr_t rip=(uintptr_t)uc->uc_mcontext.gregs[REG_RIP], sp=(uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    s->rip=rip;
    /* In a syscall: RIP right after `syscall` (0f 05), or on it when the kernel rewound it to
     * restart the call after the handler. */
    unsigned char code[4]={0,0,0,0};
    struct iovec lc={code,4}, rc={(void *)(rip-2),4};
    s->in_syscall=process_vm_readv(getpid(),&lc,1,&rc,1,0)==4 &&
                  ((code[0]==0x0f && code[1]==0x05) || (code[2]==0x0f && code[3]==0x05));
    int n=0;
    uint64_t words[512];
    /* 16 KiB, or up to 128 KiB while no eboot address was found (frames with large locals) */
    for (int chunk=0; chunk<32 && n<GP_DEPTH && (chunk<4 || n==0); ++chunk) {
        struct iovec local={words,sizeof(words)}, remote={(void *)(sp+(uintptr_t)chunk*sizeof(words)),sizeof(words)};
        /* a partial read: the stack ends there (the game's main thread runs near its top) */
        const ssize_t got=process_vm_readv(getpid(),&local,1,&remote,1,0);
        const int count=got>0 ? (int)(got/8) : 0;
        for (int i=0;i<count && n<GP_DEPTH;++i) if (words[i]-prof_base<prof_size && words[i]-prof_base>0x1000) s->ret[n++]=words[i]-prof_base;
        if (count<512) break;
    }
    while (n<GP_DEPTH) s->ret[n++]=0;
    int m=0;
    const ssize_t got=process_vm_readv(getpid(),&(struct iovec){words,sizeof(words)},1,&(struct iovec){(void *)sp,sizeof(words)},1,0);
    for (int i=0;i<(got>0 ? (int)(got/8) : 0) && m<GP_HOST;++i) if (gp_is_port(words[i])) s->host[m++]=words[i];
    while (m<GP_HOST) s->host[m++]=0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
}
static int gp_wanted(const char *name, const char *list) {
    for (const char *p=list; *p;) {
        const char *end=strchr(p,','); size_t len=end ? (size_t)(end-p) : strlen(p);
        if (len && !strncmp(name,p,len)) return 1;
        if (!end) break;
        p=end+1;
    }
    return 0;
}
/* dladdr is slow (a lookup per call): the writer caches what it found per address. */
typedef struct { uint64_t address; const char *name; uint64_t base; } GpSym;
static GpSym gp_syms[8192];
static const GpSym *gp_symbol(uint64_t address) {
    GpSym *e=&gp_syms[(address*0x9E3779B97F4A7C15ull)>>51];
    if (e->address!=address) {
        Dl_info di; memset(&di,0,sizeof(di));
        e->address=address; e->name=NULL; e->base=0;
        if (dladdr((void *)(uintptr_t)address,&di)) {
            if (di.dli_sname) { e->name=di.dli_sname; e->base=(uint64_t)(uintptr_t)di.dli_saddr; }
            else if (di.dli_fname) { const char *b=strrchr(di.dli_fname,'/'); e->name=b ? b+1 : di.dli_fname; e->base=(uint64_t)(uintptr_t)di.dli_fbase; }
        }
    }
    return e;
}
static void *gp_thread(void *arg) {
    FILE *out=arg;
    const char *list=getenv("KR_GAME_PROFILE_THREADS"); if (!list || !*list) list="main,runTaskMain,TaskJobThread";
    const char *e=getenv("KR_GAME_PROFILE_US"); long period=e && *e ? atol(e) : 1000; if (period<100) period=100;
    int named[4096]; unsigned nnamed=0;
    unsigned tick=0;
    for (;;) {
        pthread_mutex_lock(&lock);
        for (GuestThread *t=threads;t;t=t->next) {
            if (!t->tid || t->finished || !gp_wanted(t->name,list)) continue;
            unsigned k; for (k=0;k<nnamed;++k) if (named[k]==t->tid) break;
            if (k==nnamed && nnamed<4096) { named[nnamed++]=t->tid; fprintf(out,"T %d %s\n",t->tid,t->name); }
            syscall(SYS_tgkill,getpid(),t->tid,SIGRTMIN+6);
        }
        pthread_mutex_unlock(&lock);
        /* The port's own threads (not guest threads) by their host name, e.g. "bb:" for all of
         * them: listed from /proc/self/task every 256 ticks. */
        static int host_tids[64]; static int nhost;
        if ((tick & 255)==0) {
            nhost=0;
            DIR *dir=opendir("/proc/self/task");
            struct dirent *de;
            while (dir && (de=readdir(dir)) && nhost<64) {
                if (de->d_name[0]=='.') continue;
                char path[300], comm[32]={0};
                snprintf(path,sizeof(path),"/proc/self/task/%s/comm",de->d_name);
                FILE *c=fopen(path,"r");
                if (!c) continue;
                if (fgets(comm,sizeof(comm),c)) comm[strcspn(comm,"\n")]=0;
                fclose(c);
                if (strncmp(comm,"bb:",3) || !gp_wanted(comm,list)) continue;
                int tid=atoi(de->d_name);
                host_tids[nhost++]=tid;
                unsigned k; for (k=0;k<nnamed;++k) if (named[k]==tid) break;
                if (k==nnamed && nnamed<4096) { named[nnamed++]=tid; fprintf(out,"T %d %s\n",tid,comm); }
            }
            if (dir) closedir(dir);
        }
        for (int i=0;i<nhost;++i) syscall(SYS_tgkill,getpid(),host_tids[i],SIGRTMIN+6);
        if ((++tick & 63)==0 || gp_head-gp_tail>GP_SLOTS/2) {
            unsigned head=__atomic_load_n(&gp_head,__ATOMIC_ACQUIRE);
            /* the newest slots may still be written by their handlers: leave the last 256 */
            if (head-gp_tail>256) head-=256; else head=gp_tail;
            if (head-gp_tail>GP_SLOTS) gp_tail=head-GP_SLOTS;
            for (; gp_tail!=head; ++gp_tail) {
                const GpSample *s=&gp_ring[gp_tail%GP_SLOTS];
                uint64_t r=s->rip-prof_base;
                if (r<prof_size) fprintf(out,"%llu.%03llu %d %#llx %s",(unsigned long long)(s->ms_x1000/1000),(unsigned long long)(s->ms_x1000%1000),s->tid,(unsigned long long)r,s->in_syscall?"s":"-");
                else {
                    /* the port's side: its symbol (exported ones), else library+offset */
                    const GpSym *sym=gp_symbol(s->rip); char where[160];
                    if (sym->name) snprintf(where,sizeof(where),"h:%s+%#llx",sym->name,(unsigned long long)(s->rip-sym->base));
                    else snprintf(where,sizeof(where),"h%#llx",(unsigned long long)s->rip);
                    fprintf(out,"%llu.%03llu %d %s %s",(unsigned long long)(s->ms_x1000/1000),(unsigned long long)(s->ms_x1000%1000),s->tid,where,s->in_syscall?"s":"-");
                }
                for (int d=0; d<GP_DEPTH; ++d) fprintf(out," %#llx",(unsigned long long)s->ret[d]);
                for (int d=0; d<GP_HOST && s->host[d]; ++d) {
                    const GpSym *sym=gp_symbol(s->host[d]);
                    if (sym->name) fprintf(out," @%s",sym->name);
                    else fprintf(out," @?%#llx",(unsigned long long)s->host[d]);
                }
                fputc('\n',out);
            }
            fflush(out);
        }
        struct timespec ts={0,period*1000}; nanosleep(&ts,NULL);
    }
    return NULL;
}
void runtime_game_profile_start(void) {
    const char *path=getenv("KR_GAME_PROFILE");
    if (!path || !*path) return;
    FILE *out=fopen(path,"w");
    if (!out) { printf("Runtime: KR_GAME_PROFILE: cannot open %s\n",path); return; }
    struct sigaction sa={0}; sa.sa_sigaction=gp_signal; sa.sa_flags=SA_SIGINFO|SA_RESTART;
    sigemptyset(&sa.sa_mask); sigaction(SIGRTMIN+6,&sa,NULL);
    gp_start=prof_now();
    FILE *maps=fopen("/proc/self/maps","r");
    char line[512];
    while (maps && fgets(line,sizeof(line),maps) && gp_ncode<8) {
        unsigned long lo,hi; char perm[8];
        if (sscanf(line,"%lx-%lx %7s",&lo,&hi,perm)==3 && perm[2]=='x' && (strstr(line,"bb-probe") || strstr(line,"libbbgpu")))
            { gp_code[gp_ncode].lo=lo; gp_code[gp_ncode].hi=hi; ++gp_ncode; }
    }
    if (maps) fclose(maps);
    fprintf(out,"# KR_GAME_PROFILE eboot base %#llx size %#llx\n",(unsigned long long)prof_base,(unsigned long long)prof_size);
    pthread_t t; pthread_create(&t,NULL,gp_thread,out); pthread_detach(t);
    printf("Runtime: game profile sampling to %s\n",path);
}
/* kr-port: stacks are reused. bbport never released them, and this game starts a thread for every
 * streamed sound ("update ngs2 stream sound"; 31 in three minutes, 320 KiB each with the margin).
 * A joined thread's stack is free at once; a detached thread's after 2 s, past the end of its
 * host thread (glibc keeps its descriptor on a given stack until the thread is gone). */
#define STACK_POOL 128
static struct { unsigned char *memory; size_t bytes; uint64_t free_after; } stack_pool[STACK_POOL];
static size_t stacks_reused;
static void stack_release(GuestThread *t, uint64_t delay_ns) {
    if (!t->stack_memory) return;
    pthread_mutex_lock(&lock);
    for (int i=0;i<STACK_POOL;++i) if (!stack_pool[i].memory) {
        stack_pool[i].memory=t->stack_memory; stack_pool[i].bytes=t->stack_bytes;
        stack_pool[i].free_after=prof_now()+delay_ns;
        t->stack_memory=NULL;
        break;
    }
    pthread_mutex_unlock(&lock);
}
static void *stack_take(size_t bytes) {
    void *found=NULL; uint64_t now=prof_now();
    pthread_mutex_lock(&lock);
    for (int i=0;i<STACK_POOL;++i)
        if (stack_pool[i].memory && stack_pool[i].bytes==bytes && now>=stack_pool[i].free_after) {
            found=stack_pool[i].memory; stack_pool[i].memory=NULL; ++stacks_reused; break;
        }
    pthread_mutex_unlock(&lock);
    return found;
}
static int32_t create(GuestThread **out,ThreadAttr **attr_slot,GuestEntry entry,void *argument,const char *name) {
    prof_mark(name ? name : "guest");
    if (!out || !entry) return ERR(22);
    ThreadAttr *a=NULL;
    if (attr_slot && !(a=find_attr(attr_slot))) return ERR(22);
    GuestThread *t=new_thread();
    if (!t) return ERR(12);
    if (a) { t->attr=*a; t->attr.next=NULL; }
    t->entry=entry; t->argument=argument; t->detached=t->attr.detached;
    snprintf(t->name,sizeof(t->name),"%s",name ? name : "guest");
    pthread_attr_t host;
    pthread_attr_init(&host);
    uint64_t stack=t->attr.stack<MIN_STACK ? MIN_STACK : t->attr.stack;
    /* Stacks below 1 TiB as on PS4; guest code may pack stack addresses. */
    size_t stack_bytes=(size_t)stack+STACK_MARGIN;
    void *stack_memory=stack_take(stack_bytes);   /* kr-port */
    if (!stack_memory) stack_memory=runtime_low_map(stack_bytes,PROT_READ|PROT_WRITE);
    t->stack_memory=stack_memory; t->stack_bytes=stack_bytes;
#ifdef _WIN32
    pthread_attr_setstacksize(&host,256*1024); /* the host part only: the guest's is stack_memory */
#else
    if (stack_memory) pthread_attr_setstack(&host,stack_memory,stack_bytes);
    else pthread_attr_setstacksize(&host,stack_bytes);
#endif
    publish(t);
    /* Publish the handle before the thread can run and inspect itself. */
    *out=t;
    int e=pthread_create(&t->host,&host,host_start,t);
    pthread_attr_destroy(&host);
    if (e) { fprintf(stderr,"STOP: host pthread_create failed: %d\n",e); exit(21); }
    if (t->detached) pthread_detach(t->host);
    pthread_mutex_lock(&lock); ++created; pthread_mutex_unlock(&lock);
    printf("Runtime: guest thread '%s' created (stack=%llu, prio=%d)\n",t->name,(unsigned long long)stack,t->attr.prio);
    if (t->attr.stack_addr) /* kr-port */
        printf("Runtime: thread '%s': game-supplied stack %#llx replaced by a runtime stack of the same size\n",
               t->name,(unsigned long long)t->attr.stack_addr);
    return 0;
}
static ABI int32_t thread_create(GuestThread **out,ThreadAttr **attr,GuestEntry entry,void *argument,const char *name) {
    return create(out,attr,entry,argument,name);
}
static ABI int32_t thread_join(GuestThread *t,void **result) {
    if (!find_thread(t) || t->host_owned) return ERR(3);
    if (t==current) return ERR(11);
    if (t->detached || t->joined) return ERR(22);
    t->joined=1;
    void *value=NULL;
    int e=pthread_join(t->host,&value);
    if (e) { fprintf(stderr,"STOP: host pthread_join failed: %d\n",e); exit(21); }
    if (result) *result=t->result;
    pthread_mutex_lock(&lock); ++joined_count; pthread_mutex_unlock(&lock);
    stack_release(t,0);   /* kr-port */
    return 0;
}
static ABI int32_t thread_detach(GuestThread *t) {
    if (!find_thread(t)) return ERR(3);
    if (t->detached) return ERR(22);
    t->detached=1;
    if (!t->host_owned) pthread_detach(t->host);
    if (t->finished && !t->host_owned) stack_release(t,2000000000u);   /* kr-port */
    return 0;
}
static ABI __attribute__((noreturn)) void thread_exit(void *value) {
    GuestThread *t=runtime_thread_current();
    if (t->host_owned) { fputs("STOP: pthread_exit on host-owned/main thread\n",stderr); exit(21); }
    t->result=value;
    /* No host unwinder: guest frames have no registered FDEs. */
    longjmp(t->exit_jump,1);
}
/* pthread_exit for the online module's threads (gpu/bbnet20). */
__attribute__((noreturn)) void runtime_thread_exit(void *value) { thread_exit(value); }
/* kr-port: the game starts a thread per streamed sound ("update ngs2 stream sound") that polls its
 * voice in a loop (eboot 0x2d74c0, the stream object in rbx):
 *     while (state != 5) { update(); scePthreadYield(); }      state at object+0xc
 * On the console the yield gives the core to the other threads of its priority; here, with idle
 * cores, sched_yield returns at once and each stream kept a core at 100% (up to 7 at a time in
 * combat). Such a thread sleeps KR_STREAM_YIELD_US (1000; 0: the plain yield) instead, but only
 * while its stream plays (state below 4): once update() has set 5 (finished) or the stream is
 * stopping (4), the yield returns at once and the thread leaves in microseconds, as before. The
 * game reuses the stream object for its next sound and starts a new thread without joining the
 * old one, trusting it to be gone: a thread still asleep then woke on the new sound and ran
 * beside the new thread on the same object (a crash at RIP 0x1 in a long session). */
uintptr_t kr_stream_loop_return;   /* image + 0x2d74ce, set with the image (runtime_thread_profile_image) */
/* image + 0x62d655: the game's flip function (0x62d5f0) waiting for the GPU to finish with the
 * next display buffer: while (*label != 1) scePthreadYield(); */
uintptr_t kr_flip_loop_return;
/* KR_STREAM_RACE_CHECK=1 (diagnostics): counts two stream threads polling one stream object at
 * once (the race above), printed every 10 s. KR_STREAM_YIELD_GATE=0: the sleep regardless of the
 * stream's state (the behaviour before this gate, for comparison). */
static void stream_race_note(uintptr_t object) {
    static int enabled=-1;
    if (enabled<0) { const char *e=getenv("KR_STREAM_RACE_CHECK"); enabled=e && e[0]=='1'; }
    if (!enabled || !object) return;
    static struct { uintptr_t object; int tid; uint64_t at; } seen[512];
    static unsigned overlaps; static uint64_t reported;
    const int tid=current ? current->tid : 0;
    const uint64_t now=prof_now();
    unsigned slot=(unsigned)((object>>4)*0x9E3779B1u)%512;
    pthread_mutex_lock(&lock);
    int other_alive=0;
    if (seen[slot].object==object && seen[slot].tid!=tid && now-seen[slot].at<20000000u)
        for (GuestThread *g=threads;g;g=g->next) if (g->tid==seen[slot].tid && !g->finished) { other_alive=1; break; }
    /* a new thread taking over an object an old one polled: how soon after its last poll */
    static unsigned reuse[4];   /* < 1 ms, < 2 ms, < 5 ms, < 20 ms */
    if (!other_alive && seen[slot].object==object && seen[slot].tid!=tid && now-seen[slot].at<20000000u) {
        const uint64_t gap=now-seen[slot].at;
        ++reuse[gap<1000000u ? 0 : gap<2000000u ? 1 : gap<5000000u ? 2 : 3];
    }
    if (other_alive) {
        if (overlaps++<5) printf("Runtime: stream object %#lx polled by threads %d and %d at once\n",(unsigned long)object,seen[slot].tid,tid);
    }
    seen[slot].object=object; seen[slot].tid=tid; seen[slot].at=now;
    if (now-reported>10000000000u) { if (reported) printf("Runtime: stream race check: %u overlaps; object reused by a new thread after <1 ms %u, <2 ms %u, <5 ms %u, <20 ms %u\n",overlaps,reuse[0],reuse[1],reuse[2],reuse[3]); reported=now; }
    pthread_mutex_unlock(&lock);
}
ABI __attribute__((used)) int32_t kr_thread_yield_c(int mode, uintptr_t object);
ABI __attribute__((used)) int32_t kr_thread_yield_c(int mode, uintptr_t object) {
    static int gate=-1;
    if (gate<0) { const char *e=getenv("KR_STREAM_YIELD_GATE"); gate=!(e && e[0]=='0'); }
    stream_race_note(object);
    /* mode 1: the stream loop, playing; 2: the stream loop, stopping or finished; 3: the flip
     * loop; 0: elsewhere */
    const int may_sleep = mode==1 || (mode==2 && !gate);
    static int sleep_us=-1, avplayer_us=-1, flip_us=-1;
    static _Thread_local int kind=-1;   /* 1: a stream thread, 2: the game's AvPlayer thread */
    if (sleep_us<0) {
        const char *e=getenv("KR_STREAM_YIELD_US"); sleep_us=e && *e ? atoi(e) : 1000;
        e=getenv("KR_AVPLAYER_YIELD_US"); avplayer_us=e && *e ? atoi(e) : 1000;
        e=getenv("KR_FLIP_WAIT_MARGIN_US"); flip_us=e && *e ? atoi(e) : 0;
    }
    if (kind<0) kind=!current ? 0 : !strncmp(current->name,"update ngs2 stream",18) ? 1 :
                     !strncmp(current->name,"Update AvPlayer",15) ? 2 : 0;
    long us=0;
    if (kind==1 && may_sleep) us=sleep_us;
    /* The game's AvPlayer thread polls for the next movie frame in a loop with a yield (it kept a
     * core at 100% while a movie played): frames come every 33 ms, a 1 ms poll is plenty. */
    else if (kind==2) us=avplayer_us;
    /* The flip loop (rbx: the display buffer's label, 1 once the GPU has finished with it). It
     * kept half a core spinning through a wait of 5-11 ms per frame. The GPU writes that label
     * itself (no CPU hook sees it), and sleeping in fixed steps through the whole wait now and
     * then woke too late for the vblank in heavy scenes. So the wait is predicted: it sleeps
     * (steps of at most 0.5 ms, 1 us timer slack) until the shortest of the last 8 waits less
     * KR_FLIP_WAIT_MARGIN_US, then yields as before: the end of the wait, when the label can
     * turn, is spun through as on the console. Off by default (0): measured on an 8-core CPU, it
     * freed 30-40% of a core but cost about one missed vblank more every two minutes of heavy
     * combat; 1000 is for CPUs with few cores, where the freed core may be worth more. */
    else if (mode==3 && flip_us>0) {
        static _Thread_local uint64_t wait_start, wait_last, recent[8];
        static _Thread_local unsigned recent_n, recent_i;
        static _Thread_local int slack_set;
        const uint64_t now=prof_now();
        if (!wait_last || now-wait_last>1000000u) {   /* a new wait: the last one lasted until wait_last */
            if (wait_start && wait_last>wait_start) { recent[recent_i++%8]=wait_last-wait_start; if (recent_n<8) ++recent_n; }
            wait_start=now;
        }
        wait_last=now;
        if (recent_n>=4) {
            uint64_t shortest=recent[0];
            for (unsigned i=1;i<recent_n;++i) if (recent[i]<shortest) shortest=recent[i];
            const uint64_t margin=(uint64_t)flip_us*1000u, waited=now-wait_start;
            if (shortest>margin && waited+margin<shortest) {
                if (!slack_set) { prctl(PR_SET_TIMERSLACK,1000UL,0,0,0); slack_set=1; }
                const uint64_t left=shortest-margin-waited;
                us=(long)((left<500000u ? left : 500000u)/1000u);
                if (us<1) us=1;
                wait_last=now+(uint64_t)us*1000u;   /* the sleep is part of this wait */
            }
        }
    }
    if (us>0) {
        struct timespec ts={0,us*1000};
        nanosleep(&ts,NULL);
    } else sched_yield();
    return 0;
}
/* The yield's entry: may_sleep only from the stream loop (its return address) with the stream
 * playing ([rbx+0xc] < 4). */
__asm__(
    ".text\n"
    ".globl kr_thread_yield\n"
    "kr_thread_yield:\n"
    "  xor %edi, %edi\n"
    "  xor %esi, %esi\n"
    "  mov (%rsp), %rax\n"
    "  cmp kr_flip_loop_return(%rip), %rax\n"
    "  jne 2f\n"
    "  mov $3, %edi\n"
    "  mov %rbx, %rsi\n"
    "  jmp 1f\n"
    "2:\n"
    "  cmp kr_stream_loop_return(%rip), %rax\n"
    "  jne 1f\n"
    "  mov %rbx, %rsi\n"
    "  mov $2, %edi\n"
    "  cmpl $4, 0xc(%rbx)\n"
    "  jae 1f\n"
    "  mov $1, %edi\n"
    "1:\n"
    "  jmp kr_thread_yield_c\n");
int32_t kr_thread_yield(void);
#define thread_yield kr_thread_yield
static ABI int32_t thread_get_prio(GuestThread *t,int *prio) {
    if (!find_thread(t)) return ERR(3);
    if (!prio) return ERR(22);
    *prio=t->attr.prio; return 0;
}
static ABI int32_t thread_set_prio(GuestThread *t,int prio) {
    if (!find_thread(t)) return ERR(3);
    t->attr.prio=prio; return 0;
}
static ABI int32_t thread_set_affinity(GuestThread *t,uint64_t mask) {
    if (!find_thread(t)) return ERR(3);
    t->attr.affinity=mask; return 0;
}
static ABI int32_t thread_get_affinity(GuestThread *t,uint64_t *mask) {
    if (!find_thread(t)) return ERR(3);
    if (!mask) return ERR(22);
    *mask=t->attr.affinity; return 0;
}
static ABI int32_t thread_rename(GuestThread *t,const char *name) {
    if (!find_thread(t)) return ERR(3);
    if (!name) return ERR(22);
    snprintf(t->name,sizeof(t->name),"%s",name);
    if (t==current) set_host_name(t->name);
    return 0;
}
static ABI int32_t thread_equal(GuestThread *a,GuestThread *b) { return a==b; }

/* POSIX (libScePosix) variants return positive errno values. */
static int32_t posix(int32_t r) { return r ? (int32_t)((uint32_t)r&0xffff) : 0; }
static ABI int32_t posix_attr_init(ThreadAttr **a) { return posix(attr_init(a)); }
static ABI int32_t posix_attr_destroy(ThreadAttr **a) { return posix(attr_destroy(a)); }
static ABI int32_t posix_attr_set_detach(ThreadAttr **a,int s) { return posix(attr_set_detach(a,s)); }
static ABI int32_t posix_attr_set_stack(ThreadAttr **a,uint64_t s) { return posix(attr_set_stack(a,s)); }
static ABI int32_t posix_attr_set_param(ThreadAttr **a,const int *p) { return posix(attr_set_param(a,p)); }
static ABI int32_t posix_create(GuestThread **t,ThreadAttr **a,GuestEntry e,void *arg) { return posix(create(t,a,e,arg,"posix")); }
static ABI int32_t posix_create_name(GuestThread **t,ThreadAttr **a,GuestEntry e,void *arg,const char *name) { return posix(create(t,a,e,arg,name)); }
static ABI int32_t posix_join(GuestThread *t,void **r) { return posix(thread_join(t,r)); }

int32_t runtime_thread_spawn(void **out,void *(ABI *entry)(void *),void *argument,uint64_t stack,const char *name) {
    ThreadAttr *attr=NULL;
    int32_t r=attr_init(&attr);
    if (!r && stack) r=attr_set_stack(&attr,stack);
    if (!r) r=create((GuestThread **)out,&attr,entry,argument,name);
    if (attr) attr_destroy(&attr);
    return posix(r);
}
/* kr-port: scePthreadCancel. Deferred cancellation is not implemented: the request is logged
 * and the thread keeps running (games normally cancel only workers they are tearing down). */
static ABI int32_t thread_cancel(GuestThread *t) {
    if (!find_thread(t)) return ERR(3);
    fprintf(stderr,"Runtime: scePthreadCancel('%s') ignored (no deferred cancellation)\n",t->name);
    return 0;
}
int32_t runtime_thread_join_spawned(void *thread,void **result) {
    return posix(thread_join((GuestThread *)thread,result));
}

uintptr_t runtime_thread_resolve(const char *name) {
    static const struct { const char *nid; void *fn; } table[]={
        {"aI+OeCz8xrQ#p#J",thread_self}, {"EotR8a3ASf4#I#J",thread_self},
        {"9BcDykPmo1I#p#J",guest_error},
        {"nsYoNRywwNg#p#J",attr_init}, {"62KCwEMmzcM#p#J",attr_destroy},
        {"x1X76arYMxU#p#J",attr_get}, {"8+s5BzZjxSg#p#J",attr_affinity},
        {"UTXzJbWhhTE#p#J",attr_set_stack}, {"-Wreprtu0Qs#p#J",attr_set_detach},
        {"4+h9EzwKF4I#p#J",attr_set_policy}, {"DzES9hQF4f4#p#J",attr_set_param},
        {"3qxgM4ezETA#p#J",attr_set_affinity}, {"eXbUSpEaTsA#p#J",attr_set_inherit},
        {"JaRMy+QcpeU#p#J",attr_get_detach}, {"-fA+7ZlGDQs#p#J",attr_get_stack},
        {"FXPWHNk8Of0#p#J",attr_get_param}, {"El+cQ20DynU#p#J",attr_set_guard},
        /* kr-port */
        {"Bvn74vj6oLo#p#J",attr_set_stack_region}, {"qBDmpCyGssE#p#J",thread_cancel},
        {"m5-2bsNfv7s#p#J",condattr_init}, {"waPcxYiR3WA#p#J",condattr_destroy},
        {"6UgtwV+0zb4#p#J",thread_create}, {"onNY9Byn-W8#p#J",thread_join},
        {"4qGrR6eoP9Y#p#J",thread_detach}, {"3kg7rT0NQIs#p#J",thread_exit},
        {"T72hz6ffq08#p#J",thread_yield}, {"1tKyG7RlMJo#p#J",thread_get_prio},
        {"W0Hpm2X0uPE#p#J",thread_set_prio}, {"bt3CTBKmGyI#p#J",thread_set_affinity}, {"rcrVFJsQWRY#p#J",thread_get_affinity},
        {"GBUY7ywdULE#p#J",thread_rename}, {"3PtV6p3QNX4#p#J",thread_equal},
        {"wtkt-teR1so#I#J",posix_attr_init}, {"zHchY8ft5pk#I#J",posix_attr_destroy},
        {"E+tyo3lp5Lw#I#J",posix_attr_set_detach}, {"2Q0z6rnBrTE#I#J",posix_attr_set_stack},
        {"euKRgm0Vn2M#I#J",posix_attr_set_param}, {"OxhIB8LB-PQ#I#J",posix_create},
        {"Jmi+9w9u0E4#I#J",posix_create_name}, {"h9CcP3J0oVM#I#J",posix_join},
        {"FJrT5LuUBAU#I#J",thread_exit},
    };
    for (size_t i=0;i<sizeof(table)/sizeof(*table);++i)
        if (!strcmp(name,table[i].nid)) return (uintptr_t)table[i].fn;
    return 0;
}
void runtime_thread_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: guest threads created=%zu, exited=%zu, joined=%zu, stacks reused=%zu\n",created,exited,joined_count,stacks_reused);
    pthread_mutex_unlock(&lock);
}
