// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: libSceAudiodec, first step: a SILENT decoder so the game can boot.
 * No reference project implements this library. The layout below is the one the game itself
 * builds (eboot v1.02, function at Ghidra 0x3dc740): a control block of four pointers
 *   +0x00 codec parameters  (uiSize first; 8 bytes for decoder type 2, 0x18 for type 3)
 *   +0x08 stream info       (uiSize first; 0x14)
 *   +0x10 access unit       { u32 size; void *addr; u32 bytes; }   (0x18)
 *   +0x18 PCM output item   { u32 size; void *addr; u32 bytes; }   (0x18)
 * sceAudiodecCreateDecoder(ctrl, type) returns a handle (>= 0). Decode consumes the whole access
 * unit and reports the whole PCM buffer as written silence. Real decoding (ATRAC9 through
 * LibAtrac9, which is already linked) replaces this together with the Ngs2 mixer. */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Error values: not verified against the SDK (only returned on misuse; the game's paths succeed). */
#define ERR_INVALID_TYPE   ((int32_t)0x807F0001)
#define ERR_INVALID_HANDLE ((int32_t)0x807F0002)
#define ERR_INVALID_PARAM  ((int32_t)0x807F0003)
#define ERR_NO_HANDLE      ((int32_t)0x807F0004)
#define MAX_DECODERS 64

typedef struct { uint32_t size; uint32_t pad; void *addr; uint32_t bytes; uint32_t pad2; } Item;
typedef struct { void *param; void *info; Item *au; Item *pcm; } Ctrl;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t decoder_type[MAX_DECODERS];   /* 0 = free */
static unsigned libraries;
static size_t created, decodes, au_bytes;

static ABI int32_t init_library(uint32_t type) {
    if (!type) return ERR_INVALID_TYPE;
    pthread_mutex_lock(&lock); libraries |= 1u << (type & 31); pthread_mutex_unlock(&lock);
    printf("Runtime: Audiodec library type %u initialized (silent decoder)\n", type);
    return 0;
}
static ABI int32_t term_library(uint32_t type) {
    pthread_mutex_lock(&lock); libraries &= ~(1u << (type & 31)); pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t create_decoder(const Ctrl *ctrl, uint32_t type) {
    if (!ctrl || !ctrl->param) return ERR_INVALID_PARAM;
    if (!type) return ERR_INVALID_TYPE;
    pthread_mutex_lock(&lock);
    int32_t handle = ERR_NO_HANDLE;
    for (int i = 0; i < MAX_DECODERS; ++i)
        if (!decoder_type[i]) { decoder_type[i] = type; handle = i; ++created; break; }
    pthread_mutex_unlock(&lock);
    return handle;
}
static ABI int32_t delete_decoder(int32_t handle) {
    if (handle < 0 || handle >= MAX_DECODERS) return ERR_INVALID_HANDLE;
    pthread_mutex_lock(&lock);
    int32_t e = decoder_type[handle] ? 0 : ERR_INVALID_HANDLE;
    decoder_type[handle] = 0;
    pthread_mutex_unlock(&lock);
    return e;
}
static ABI int32_t decode(int32_t handle, Ctrl *ctrl) {
    if (handle < 0 || handle >= MAX_DECODERS) return ERR_INVALID_HANDLE;
    if (!ctrl || !ctrl->au || !ctrl->pcm) return ERR_INVALID_PARAM;
    pthread_mutex_lock(&lock);
    int ok = decoder_type[handle] != 0;
    if (ok) { ++decodes; au_bytes += ctrl->au->bytes; }
    pthread_mutex_unlock(&lock);
    if (!ok) return ERR_INVALID_HANDLE;
    static int trace = -1, lines;
    if (trace < 0) { const char *e = getenv("KR_TRACE_NGS2"); trace = e && *e == '1'; }
    if (trace && lines++ < 40) {
        const uint32_t *pp = ctrl->param, *in = ctrl->info;
        printf("AUDIODEC h=%d type=%u param %08x %08x %08x %08x info %08x %08x %08x %08x %08x au=%u pcm=%u\n",
               handle, decoder_type[handle], pp[0], pp[1], pp[2], pp[3], in ? in[0] : 0, in ? in[1] : 0,
               in ? in[2] : 0, in ? in[3] : 0, in ? in[4] : 0, ctrl->au->bytes, ctrl->pcm->bytes);
    }
    if (ctrl->pcm->addr && ctrl->pcm->bytes) memset(ctrl->pcm->addr, 0, ctrl->pcm->bytes);
    return 0;   /* au->bytes stays: the whole unit is consumed; pcm->bytes: all of it written */
}

uintptr_t runtime_audiodec_resolve(const char *name) {
    if (runtime_nid_is(name, "VjhsmxpcezI")) return (uintptr_t)init_library;
    if (runtime_nid_is(name, "h5jSB2QIDV0")) return (uintptr_t)term_library;
    if (runtime_nid_is(name, "O3f1sLMWRvs")) return (uintptr_t)create_decoder;
    if (runtime_nid_is(name, "Tp+ZEy69mLk")) return (uintptr_t)delete_decoder;
    if (runtime_nid_is(name, "KHXHMDLkILw")) return (uintptr_t)decode;
    return 0;
}
void runtime_audiodec_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: Audiodec (silent) decoders created=%zu, decodes=%zu, input=%zu bytes\n", created, decodes, au_bytes);
    pthread_mutex_unlock(&lock);
}
