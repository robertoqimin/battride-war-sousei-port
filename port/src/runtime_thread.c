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
#include <signal.h>
#include <sys/syscall.h>
#include <sys/uio.h>
static uintptr_t prof_base; static uint64_t prof_size;
static volatile pid_t prof_tid; static volatile uint64_t prof_until;
static char prof_label[64];
#define PROF_SLOTS 65536
#define PROF_DEPTH 6
static uint64_t prof_pc[PROF_SLOTS][PROF_DEPTH]; static int prof_who[PROF_SLOTS]; static volatile unsigned prof_count;
void runtime_thread_profile_image(uintptr_t base, uint64_t size) { prof_base=base; prof_size=size; }
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
 * voice in a loop: while (!done) { update(); scePthreadYield(); }. On the console the yield gives
 * the core to the other threads of its priority; here, with idle cores, sched_yield returns at
 * once and each stream kept a core at 100% (up to 7 at a time in combat). Those threads sleep
 * KR_STREAM_YIELD_US (1000; 0: the plain yield) instead: the voice is fed in 8 KiB pieces and the
 * mixer renders a grain every ~5 ms, so a 1 ms poll keeps it fed. */
static ABI int32_t thread_yield(void) {
    static int sleep_us=-1;
    static _Thread_local int stream_poller=-1;
    if (sleep_us<0) { const char *e=getenv("KR_STREAM_YIELD_US"); sleep_us=e && *e ? atoi(e) : 1000; }
    if (stream_poller<0) stream_poller=current && !strncmp(current->name,"update ngs2 stream",18);
    if (stream_poller && sleep_us>0) {
        struct timespec ts={0,(long)sleep_us*1000};
        nanosleep(&ts,NULL);
    } else sched_yield();
    return 0;
}
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
