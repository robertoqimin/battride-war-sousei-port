// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: libSceZlib (asynchronous zlib-format inflate), which Bloodborne never uses.
 * Interface and error codes as in shadPS4's src/core/libraries/zlib (GPL-2.0-or-later).
 * The PS4 decodes on a worker and reports finished requests through a done queue; here
 * sceZlibInflate decodes at once with the host zlib and queues the result, so
 * sceZlibWaitForDone only has to take the oldest finished request. Results stay until
 * sceZlibGetResult reads them. */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#define ZERR_NOT_FOUND           ((int32_t)0x81120002)
#define ZERR_INVALID             ((int32_t)0x81120016)
#define ZERR_NOSPACE             ((int32_t)0x8112001C)
#define ZERR_TIMEDOUT            ((int32_t)0x81120027)
#define ZERR_NOT_INITIALIZED     ((int32_t)0x81120032)
#define ZERR_ALREADY_INITIALIZED ((int32_t)0x81120033)
#define ZERR_FATAL               ((int32_t)0x811200FF)

typedef struct Request {
    uint64_t id;
    uint32_t length;
    int32_t status;
    int waited;              /* already returned by WaitForDone */
    struct Request *next;    /* oldest first */
} Request;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized;
static uint64_t next_id = 1;
static Request *requests;
static size_t inflates, failures, bytes_in, bytes_out;

static ABI int32_t zlib_initialize(const void *buffer, uint32_t length) {
    (void)buffer; (void)length;   /* the game's work buffer: the host zlib needs none */
    pthread_mutex_lock(&lock);
    int32_t r = initialized ? ZERR_ALREADY_INITIALIZED : 0;
    initialized = 1;
    pthread_mutex_unlock(&lock);
    if (!r) printf("Runtime: sceZlib initialized (host zlib %s)\n", zlibVersion());
    return r;
}
static ABI int32_t zlib_finalize(void) {
    pthread_mutex_lock(&lock);
    int32_t r = initialized ? 0 : ZERR_NOT_INITIALIZED;
    initialized = 0;
    while (requests) { Request *q = requests; requests = q->next; free(q); }
    pthread_mutex_unlock(&lock);
    return r;
}
static ABI int32_t zlib_inflate(const void *src, uint32_t src_len, void *dst, uint32_t dst_len,
                                uint64_t *request_id) {
    if (!src || !src_len || !dst || !dst_len || !request_id) return ZERR_INVALID;
    pthread_mutex_lock(&lock);
    int ready = initialized;
    pthread_mutex_unlock(&lock);
    if (!ready) return ZERR_NOT_INITIALIZED;
    uLongf out = dst_len;
    int ret = uncompress(dst, &out, src, src_len);
    Request *q = calloc(1, sizeof(*q));
    if (!q) return ZERR_FATAL;
    q->length = (uint32_t)out;
    q->status = ret == Z_OK ? 0 : ret == Z_BUF_ERROR ? ZERR_NOSPACE : ZERR_FATAL;
    pthread_mutex_lock(&lock);
    q->id = next_id++;
    Request **tail = &requests;
    while (*tail) tail = &(*tail)->next;
    *tail = q;
    ++inflates; bytes_in += src_len; bytes_out += out;
    if (q->status) ++failures;
    *request_id = q->id;
    pthread_mutex_unlock(&lock);
    if (q->status) fprintf(stderr, "Runtime: sceZlibInflate failed (zlib %d, %u bytes in)\n", ret, src_len);
    return 0;
}
static ABI int32_t zlib_wait_for_done(uint64_t *request_id, const uint32_t *timeout) {
    if (!request_id) return ZERR_INVALID;
    pthread_mutex_lock(&lock);
    int32_t r = 0;
    if (!initialized) r = ZERR_NOT_INITIALIZED;
    else {
        Request *q = requests;
        while (q && q->waited) q = q->next;
        if (q) { q->waited = 1; *request_id = q->id; }
        else r = ZERR_TIMEDOUT;   /* nothing is pending: every request finishes inside Inflate */
        (void)timeout;
    }
    pthread_mutex_unlock(&lock);
    return r;
}
static ABI int32_t zlib_get_result(uint64_t request_id, uint32_t *dst_length, int32_t *status) {
    if (!dst_length || !status) return ZERR_INVALID;
    pthread_mutex_lock(&lock);
    int32_t r = ZERR_NOT_FOUND;
    if (!initialized) r = ZERR_NOT_INITIALIZED;
    else for (Request **p = &requests; *p; p = &(*p)->next) {
        if ((*p)->id != request_id) continue;
        Request *q = *p;
        *dst_length = q->length; *status = q->status;
        *p = q->next; free(q); r = 0;
        break;
    }
    pthread_mutex_unlock(&lock);
    return r;
}

uintptr_t runtime_zlib_resolve(const char *name) {
    if (runtime_nid_is(name, "m1YErdIXCp4")) return (uintptr_t)zlib_initialize;
    if (runtime_nid_is(name, "6na+Sa-B83w")) return (uintptr_t)zlib_finalize;
    if (runtime_nid_is(name, "TLar1HULv1Q")) return (uintptr_t)zlib_inflate;
    if (runtime_nid_is(name, "uB8VlDD4e0s")) return (uintptr_t)zlib_wait_for_done;
    if (runtime_nid_is(name, "2eDcGHC0YaM")) return (uintptr_t)zlib_get_result;
    return 0;
}
void runtime_zlib_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: zlib inflates=%zu, failures=%zu, in=%zu bytes, out=%zu bytes\n",
           inflates, failures, bytes_in, bytes_out);
    pthread_mutex_unlock(&lock);
}
