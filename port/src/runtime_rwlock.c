// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Linux rwlocks behind PS4 pointer-to-handle ABI. The registry protects
   lifetime, static initialization and ownership; waiting happens outside it. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
typedef struct Holder {
    pthread_t thread;
    unsigned readers, writer, pending;
    struct Holder *next;
} Holder;
typedef struct Rwlock {
    pthread_rwlock_t native;
    pthread_mutex_t meta; /* kr-port: holders and inflight (was the registry's lock) */
    Holder *holders;
    unsigned inflight;
    struct Rwlock *next;
} Rwlock;
typedef struct { int64_t seconds, nanoseconds; } GuestTime;
/* kr-port: the registry is a hash of the live handles (it was one list searched on every lock
 * and unlock under one mutex: with thousands of the game's rwlocks, spawning an army of enemies
 * took 100 ms). A read lock on it finds a handle; the handle's own mutex is taken before that
 * read lock is let go, so a destroy (which takes the registry for writing) cannot free it under
 * a waiter. */
static pthread_rwlock_t registry_lock = PTHREAD_RWLOCK_INITIALIZER;
#define BUCKETS 16384
static Rwlock *registry[BUCKETS];
static size_t created, live, reads, writes, unlocks;
static size_t bucket(const void *p) { uintptr_t v=(uintptr_t)p; v^=v>>17; v*=0x9E3779B97F4A7C15ull; return (size_t)(v>>40)%BUCKETS; }
static int32_t error(int e) {
    if (!e) return 0;
    unsigned code;
    switch(e) {
    case EPERM: code=1; break;
    case EDEADLK: code=11; break;
    case ENOMEM: code=12; break;
    case EBUSY: code=16; break;
    case EINVAL: code=22; break;
    case EAGAIN: code=35; break;
    case ETIMEDOUT: code=60; break;
    default: fprintf(stderr,"STOP: unmapped rwlock error %d\n",e); exit(21);
    }
    return (int32_t)(UINT32_C(0x80020000)|code);
}
/* find_locked and create_locked need registry_lock (create: for writing). */
static Rwlock *find_locked(Rwlock *handle) {
    for (Rwlock *r=registry[bucket(handle)]; r; r=r->next) if (r==handle) return r;
    return NULL;
}
static int create_locked(Rwlock **out) {
    Rwlock *r=calloc(1,sizeof(*r));
    if (!r) return ENOMEM;
    int e=pthread_rwlock_init(&r->native,NULL);
    if (e) { free(r); return e; }
    pthread_mutex_init(&r->meta,NULL);
    Rwlock **head=&registry[bucket(r)];
    r->next=*head; *head=r; *out=r; ++created; ++live;
    static size_t next_note=1024;
    if (created>=next_note) { printf("Runtime: rwlocks: %zu created, %zu live\n",created,live); next_note*=2; }
    return 0;
}
static ABI int32_t rw_init(Rwlock **out, void *const *attr, const char *name) {
    (void)name;
    if (!out || (attr && !*attr)) return error(EINVAL);
    if (attr) { fputs("STOP: non-default PS4 rwlock attributes are not implemented\n",stderr); exit(21); }
    pthread_rwlock_wrlock(&registry_lock);
    /* POSIX allows uninitialized storage, so never read *out during init. */
    int e=create_locked(out);
    pthread_rwlock_unlock(&registry_lock); return error(e);
}
/* The handle's lock with its meta mutex held (0), or an errno. A static initializer (NULL) is
 * created here. */
static int lookup(Rwlock **handle, Rwlock **out) {
    pthread_rwlock_rdlock(&registry_lock);
    Rwlock *r=*handle ? find_locked(*handle) : NULL;
    if (!*handle) {
        pthread_rwlock_unlock(&registry_lock);
        pthread_rwlock_wrlock(&registry_lock);
        int e=*handle ? 0 : create_locked(handle);
        if (e) { pthread_rwlock_unlock(&registry_lock); return e; }
        r=find_locked(*handle);
    }
    if (!r) { pthread_rwlock_unlock(&registry_lock); return EINVAL; }
    pthread_mutex_lock(&r->meta);
    pthread_rwlock_unlock(&registry_lock);
    *out=r; return 0;
}
/* Helpers ending in _locked below need the lock's meta mutex. */
static void prune_locked(Rwlock *r, Holder *h) {
    if (h->readers || h->writer || h->pending) return;
    Holder **link=&r->holders;
    while (*link!=h) link=&(*link)->next;
    *link=h->next; free(h);
}
enum Operation { READ, WRITE, TRY_READ, TRY_WRITE, TIMED_READ, TIMED_WRITE };
static int32_t acquire(Rwlock **handle, enum Operation op, const GuestTime *time) {
    if (!handle) return error(EINVAL);
    int writer=op==WRITE || op==TRY_WRITE || op==TIMED_WRITE;
    int attempt=op==TRY_READ || op==TRY_WRITE;
    int timed=op==TIMED_READ || op==TIMED_WRITE;
    Rwlock *r;
    int e=lookup(handle,&r);
    if (e) return error(e);
    Holder *h=r->holders;
    while (h && !pthread_equal(h->thread,pthread_self())) h=h->next;
    if (h && (h->writer || (writer && h->readers))) {
        pthread_mutex_unlock(&r->meta); return error(attempt ? EBUSY : EDEADLK);
    }
    if (h && h->readers==UINT_MAX) { pthread_mutex_unlock(&r->meta); return error(EAGAIN); }
    if (!h) {
        h=calloc(1,sizeof(*h));
        if (!h) { pthread_mutex_unlock(&r->meta); return error(ENOMEM); }
        h->thread=pthread_self(); h->next=r->holders; r->holders=h;
    }
    ++r->inflight; ++h->pending;
    pthread_mutex_unlock(&r->meta);
    /* Timed operations first try the lock, matching POSIX timeout validation. */
    if (attempt || timed) e=writer ? pthread_rwlock_trywrlock(&r->native) : pthread_rwlock_tryrdlock(&r->native);
    else e=writer ? pthread_rwlock_wrlock(&r->native) : pthread_rwlock_rdlock(&r->native);
    if (timed && e==EBUSY) {
        if (!time || time->nanoseconds<0 || time->nanoseconds>=1000000000) e=EINVAL;
        else {
            struct timespec deadline={.tv_sec=(time_t)time->seconds,.tv_nsec=(long)time->nanoseconds};
            e=writer ? pthread_rwlock_timedwrlock(&r->native,&deadline) : pthread_rwlock_timedrdlock(&r->native,&deadline);
        }
    }
    pthread_mutex_lock(&r->meta);
    --r->inflight; --h->pending;
    if (!e) {
        if (writer) { h->writer=1; __atomic_add_fetch(&writes,1,__ATOMIC_RELAXED); }
        else { ++h->readers; __atomic_add_fetch(&reads,1,__ATOMIC_RELAXED); }
    }
    prune_locked(r,h);
    pthread_mutex_unlock(&r->meta); return error(e);
}
static ABI int32_t rw_read(Rwlock **r) { return acquire(r,READ,NULL); }
static ABI int32_t rw_write(Rwlock **r) { return acquire(r,WRITE,NULL); }
static ABI int32_t rw_tryread(Rwlock **r) { return acquire(r,TRY_READ,NULL); }
static ABI int32_t rw_trywrite(Rwlock **r) { return acquire(r,TRY_WRITE,NULL); }
static ABI int32_t rw_timedread(Rwlock **r,const GuestTime *t) { return acquire(r,TIMED_READ,t); }
static ABI int32_t rw_timedwrite(Rwlock **r,const GuestTime *t) { return acquire(r,TIMED_WRITE,t); }
static ABI int32_t rw_unlock(Rwlock **handle) {
    if (!handle) return error(EINVAL);
    if (!*handle) return error(EINVAL);
    Rwlock *r;
    int e=lookup(handle,&r);
    if (e) return error(e);
    Holder *h=r->holders;
    while (h && !pthread_equal(h->thread,pthread_self())) h=h->next;
    if (!h || (!h->readers && !h->writer)) { pthread_mutex_unlock(&r->meta); return error(EPERM); }
    e=pthread_rwlock_unlock(&r->native);
    if (!e) { if (h->writer) h->writer=0; else --h->readers; __atomic_add_fetch(&unlocks,1,__ATOMIC_RELAXED); }
    prune_locked(r,h);
    pthread_mutex_unlock(&r->meta); return error(e);
}
static ABI int32_t rw_destroy(Rwlock **handle) {
    if (!handle) return error(EINVAL);
    pthread_rwlock_wrlock(&registry_lock);
    if (!*handle) { pthread_rwlock_unlock(&registry_lock); return 0; }
    Rwlock *r=find_locked(*handle);
    if (!r) { pthread_rwlock_unlock(&registry_lock); return error(EINVAL); }
    pthread_mutex_lock(&r->meta);
    const int busy=r->inflight || r->holders;
    pthread_mutex_unlock(&r->meta);
    if (busy) { pthread_rwlock_unlock(&registry_lock); return error(EBUSY); }
    int e=pthread_rwlock_destroy(&r->native);
    if (!e) {
        Rwlock **link=&registry[bucket(r)];
        while (*link!=r) link=&(*link)->next;
        *link=r->next; pthread_mutex_destroy(&r->meta); free(r); *handle=(Rwlock *)(uintptr_t)1; --live;
    }
    pthread_rwlock_unlock(&registry_lock); return error(e);
}
uintptr_t runtime_rwlock_resolve(const char *name) {
    if (runtime_nid_is(name, "6ULAa0fq4jA")) return (uintptr_t)rw_init;
    if (runtime_nid_is(name, "BB+kb08Tl9A")) return (uintptr_t)rw_destroy;
    if (runtime_nid_is(name, "Ox9i0c7L5w0")) return (uintptr_t)rw_read;
    if (runtime_nid_is(name, "mqdNorrB+gI")) return (uintptr_t)rw_write;
    if (runtime_nid_is(name, "XD3mDeybCnk")) return (uintptr_t)rw_tryread;
    if (runtime_nid_is(name, "bIHoZCTomsI")) return (uintptr_t)rw_trywrite;
    if (runtime_nid_is(name, "+L98PIbGttk")) return (uintptr_t)rw_unlock;
    if (runtime_nid_is(name, "iPtZRWICjrM")) return (uintptr_t)rw_timedread;
    if (runtime_nid_is(name, "adh--6nIqTk")) return (uintptr_t)rw_timedwrite;
    return 0;
}
void runtime_rwlock_report(void) {
    pthread_rwlock_rdlock(&registry_lock);
    printf("Runtime: rwlocks created=%zu, live=%zu, reads=%zu, writes=%zu, unlocks=%zu\n",created,live,reads,writes,unlocks);
    pthread_rwlock_unlock(&registry_lock);
}
