// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: kernel event flags (sceKernel*EventFlag), which Bloodborne never uses.
 * Semantics follow shadPS4's src/core/libraries/kernel/threads/event_flag.cpp
 * (GPL-2.0-or-later), rewritten for this runtime's C style:
 *   - a 64-bit pattern; waiters ask for AND (all bits) or OR (any bit);
 *   - a satisfied waiter may clear all bits or only its own bits;
 *   - SINGLE flags allow one waiter at a time (EPERM otherwise), MULTI many;
 *   - Clear keeps only the bits given (pattern &= bits), as on the PS4;
 *   - Cancel sets the pattern and wakes every waiter with ECANCELED,
 *     Delete wakes them with EACCES.
 * Priority-ordered queues (TH_PRIO) are served FIFO, like this runtime's semaphores.
 * The handle given to the game is the host pointer of the flag, as on the PS4,
 * where SceKernelEventFlag is an opaque pointer. */
#define _GNU_SOURCE
#include "runtime.h"
#include "../gpu/shim/bb_env.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
enum { EPERM_=1, ESRCH_=3, EACCES_=13, EBUSY_=16, EINVAL_=22, ETIMEDOUT_=60,
       ENAMETOOLONG_=63, ECANCELED_=85 };
enum { ATTR_TH_FIFO=0x01, ATTR_TH_PRIO=0x02, ATTR_SINGLE=0x10, ATTR_MULTI=0x20 };
enum { WAIT_AND=0x01, WAIT_OR=0x02, WAIT_CLEAR_ALL=0x10, WAIT_CLEAR_PAT=0x20 };
#define FLAG_MAGIC UINT32_C(0x45564631) /* "EVF1": rejects stale or foreign handles */

typedef struct Waiter {
    uint64_t bits, result;
    uint32_t mode;
    int32_t status;   /* 0 while waiting, else the final result */
    int done;
    pthread_cond_t event;
    struct Waiter *next;
} Waiter;
typedef struct Flag {
    uint32_t magic, attr;
    uint64_t bits;
    char name[32];
    unsigned active;  /* waiters still inside wait() */
    int deleted;
    Waiter *first;
    struct Flag *next;
} Flag;

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Flag *registry;
static size_t created, deleted, sets, clears, waits, polls, wakeups, timed_out, cancels;

static void host_check(int e) {
    if (e) { fprintf(stderr,"STOP: event flag host error %d\n",e); exit(21); }
}
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(RUNTIME_COND_CLOCK,&t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
/* Caller holds lock. Only flags still in the registry are valid handles. */
static Flag *find(const void *handle) {
    for (Flag *f=registry;f;f=f->next) if (f==handle && f->magic==FLAG_MAGIC) return f;
    return NULL;
}
static int mode_valid(uint32_t mode) {
    uint32_t wait=mode&0xf, clear=mode&0xf0;
    if (mode & ~UINT32_C(0xff)) return 0;
    if (wait!=WAIT_AND && wait!=WAIT_OR) return 0;
    return clear==0 || clear==WAIT_CLEAR_ALL || clear==WAIT_CLEAR_PAT;
}
static int satisfied(uint64_t have,uint64_t want,uint32_t mode) {
    return (mode&0xf)==WAIT_AND ? (have&want)==want : (have&want)!=0;
}
static void apply_clear(Flag *f,uint64_t want,uint32_t mode) {
    if ((mode&0xf0)==WAIT_CLEAR_ALL) f->bits=0;
    else if ((mode&0xf0)==WAIT_CLEAR_PAT) f->bits&=~want;
}
/* Caller holds lock: wake every waiter whose condition now holds, in queue order. */
static void release_satisfied(Flag *f) {
    Waiter **p=&f->first;
    while (*p) {
        Waiter *w=*p;
        if (!satisfied(f->bits,w->bits,w->mode)) { p=&w->next; continue; }
        w->result=f->bits;
        apply_clear(f,w->bits,w->mode);
        *p=w->next; w->done=1; ++wakeups;
        host_check(pthread_cond_signal(&w->event));
    }
}
static void wake_all(Flag *f,int32_t status,uint64_t result) {
    while (f->first) {
        Waiter *w=f->first; f->first=w->next;
        w->status=status; w->result=result; w->done=1;
        host_check(pthread_cond_signal(&w->event));
    }
}

static ABI int32_t evf_create(void **out,const char *name,uint32_t attr,uint64_t init,
                              const void *options) {
    if (!out || !name) return ERR(EINVAL_);
    if (options || attr>(ATTR_MULTI|ATTR_TH_PRIO)) return ERR(EINVAL_);
    uint32_t queue=attr&0xf, threads=attr&0xf0;
    if (queue>ATTR_TH_PRIO || (threads && threads!=ATTR_SINGLE && threads!=ATTR_MULTI))
        return ERR(EINVAL_);
    size_t length=strlen(name);
    if (length>=sizeof(((Flag *)0)->name)) return ERR(ENAMETOOLONG_);
    Flag *f=calloc(1,sizeof(*f));
    if (!f) return ERR(12);
    f->magic=FLAG_MAGIC; f->attr=attr; f->bits=init;
    memcpy(f->name,name,length+1);
    pthread_mutex_lock(&lock);
    f->next=registry; registry=f; ++created;
    pthread_mutex_unlock(&lock);
    *out=f;
    printf("Runtime: event flag \"%s\" attr=%#x init=%#llx\n",name,attr,(unsigned long long)init);
    return 0;
}
static ABI int32_t evf_delete(void *handle) {
    pthread_mutex_lock(&lock);
    Flag **p=&registry;
    while (*p && *p!=handle) p=&(*p)->next;
    if (!*p || (*p)->magic!=FLAG_MAGIC) { pthread_mutex_unlock(&lock); return ERR(ESRCH_); }
    Flag *f=*p; *p=f->next; f->deleted=1; ++deleted;
    wake_all(f,ERR(EACCES_),f->bits);
    if (!f->active) { f->magic=0; free(f); }
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t evf_set(void *handle,uint64_t bits) {
    pthread_mutex_lock(&lock);
    Flag *f=find(handle);
    if (!f) { pthread_mutex_unlock(&lock); return ERR(ESRCH_); }
    f->bits|=bits; ++sets;
    release_satisfied(f);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t evf_clear(void *handle,uint64_t keep) {
    pthread_mutex_lock(&lock);
    Flag *f=find(handle);
    if (!f) { pthread_mutex_unlock(&lock); return ERR(ESRCH_); }
    f->bits&=keep; ++clears;
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t evf_cancel(void *handle,uint64_t pattern,int32_t *waiters) {
    pthread_mutex_lock(&lock);
    Flag *f=find(handle);
    if (!f) { pthread_mutex_unlock(&lock); return ERR(ESRCH_); }
    int32_t n=0;
    for (Waiter *w=f->first;w;w=w->next) ++n;
    if (waiters) *waiters=n;
    f->bits=pattern; ++cancels;
    wake_all(f,ERR(ECANCELED_),pattern);
    pthread_mutex_unlock(&lock);
    return 0;
}
static int32_t wait_bits(void *handle,uint64_t want,uint32_t mode,uint64_t *result,
                         uint32_t *timeout,int block) {
    if (!want || !mode_valid(mode)) return ERR(EINVAL_);
    pthread_mutex_lock(&lock);
    Flag *f=find(handle);
    int32_t status=0;
    if (!f) status=ERR(ESRCH_);
    else if ((f->attr&0xf0)!=ATTR_MULTI && f->first) status=ERR(EPERM_);
    else if (satisfied(f->bits,want,mode)) {
        if (result) *result=f->bits;
        apply_clear(f,want,mode);
        if (block) ++waits; else ++polls;
    } else if (!block) {
        if (result) *result=f->bits;
        status=ERR(EBUSY_); ++polls;
    } else if (timeout && !*timeout) {
        if (result) *result=f->bits;
        status=ERR(ETIMEDOUT_); ++timed_out; ++waits;
    } else {
        if (bb_env("KR_TRACE_EVF"))
            fprintf(stderr,"Runtime: blocking wait on event flag \"%s\" (want %#llx, mode %#x, have %#llx)\n",
                    f->name,(unsigned long long)want,mode,(unsigned long long)f->bits);
        Waiter w={.bits=want,.mode=mode};
        pthread_condattr_t attr;
        host_check(pthread_condattr_init(&attr));
        host_check(pthread_condattr_setclock(&attr,RUNTIME_COND_CLOCK));
        host_check(pthread_cond_init(&w.event,&attr));
        host_check(pthread_condattr_destroy(&attr));
        Waiter **tail=&f->first;
        while (*tail) tail=&(*tail)->next;
        *tail=&w; ++f->active; ++waits;
        const uint64_t wait_start=runtime_wait_clock();
        uint64_t deadline=timeout ? now_ns()+(uint64_t)*timeout*1000 : 0;
        struct timespec end={.tv_sec=(time_t)(deadline/1000000000),.tv_nsec=(long)(deadline%1000000000)};
        while (!w.done) {
            int e=timeout ? pthread_cond_timedwait(&w.event,&lock,&end) : pthread_cond_wait(&w.event,&lock);
            if (e==ETIMEDOUT && !w.done) {
                Waiter **p=&f->first;
                while (*p!=&w) p=&(*p)->next;
                *p=w.next; w.done=1; w.status=ERR(ETIMEDOUT_); w.result=f->bits; ++timed_out;
            } else if (e!=ETIMEDOUT) host_check(e);
        }
        runtime_wait_note(2,runtime_wait_clock()-wait_start);
        status=w.status;
        if (result) *result=w.result;
        if (timeout) {
            uint64_t now=now_ns();
            *timeout=status ? 0 : (uint32_t)(now>=deadline ? 0 : (deadline-now)/1000);
        }
        host_check(pthread_cond_destroy(&w.event));
        if (!--f->active && f->deleted) { f->magic=0; free(f); }
    }
    pthread_mutex_unlock(&lock);
    return status;
}
static ABI int32_t evf_wait(void *handle,uint64_t bits,uint32_t mode,uint64_t *result,uint32_t *timeout) {
    return wait_bits(handle,bits,mode,result,timeout,1);
}
static ABI int32_t evf_poll(void *handle,uint64_t bits,uint32_t mode,uint64_t *result) {
    return wait_bits(handle,bits,mode,result,NULL,0);
}
/* Named (inter-process) event flags: no other process exists here. */
static ABI int32_t evf_open(void) { return ERR(ESRCH_); }
static ABI int32_t evf_close(void) { return 0; }

uintptr_t runtime_eventflag_resolve(const char *name) {
    if (runtime_nid_is(name, "BpFoboUJoZU")) return (uintptr_t)evf_create;
    if (runtime_nid_is(name, "8mql9OcQnd4")) return (uintptr_t)evf_delete;
    if (runtime_nid_is(name, "IOnSvHzqu6A")) return (uintptr_t)evf_set;
    if (runtime_nid_is(name, "7uhBFWRAS60")) return (uintptr_t)evf_clear;
    if (runtime_nid_is(name, "PZku4ZrXJqg")) return (uintptr_t)evf_cancel;
    if (runtime_nid_is(name, "JTvBflhYazQ")) return (uintptr_t)evf_wait;
    if (runtime_nid_is(name, "9lvj5DjHZiA")) return (uintptr_t)evf_poll;
    if (runtime_nid_is(name, "1vDaenmJtyA")) return (uintptr_t)evf_open;
    if (runtime_nid_is(name, "s9-RaxukuzQ")) return (uintptr_t)evf_close;
    return 0;
}
void runtime_eventflag_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: event flags created=%zu, deleted=%zu, sets=%zu, clears=%zu, waits=%zu, polls=%zu, "
           "wakeups=%zu, timeouts=%zu, cancels=%zu\n",
           created,deleted,sets,clears,waits,polls,wakeups,timed_out,cancels);
    pthread_mutex_unlock(&lock);
}
