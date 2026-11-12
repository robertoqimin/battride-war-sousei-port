// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: libSceNgs2, the mixer this game plays all of its sound through (BGM, effects, voices:
 * ATRAC9 .at9 files, which the game loads whole and hands to sampler voices).
 *
 * What is implemented, from the game's own use (KR_TRACE_NGS2=1 traces it) and its code:
 *   - sceNgs2ParseWaveformData: RIFF fmt/fact/smpl -> SceNgs2WaveformInfo with the blocks the
 *     game copies into its voices (one block, or intro + an endlessly repeated loop block);
 *   - sampler voices (rack 0x1000): setup (format), waveform blocks, pitch, exit loop; voice
 *     parameters: matrix levels, port matrix, events play/stop/kill/pause/resume;
 *   - sceNgs2SystemRender: every playing sampler voice of the system decoded (LibAtrac9; 16-bit
 *     PCM also), resampled (pitch x sample rate, linear) and mixed through its matrix into the
 *     float render buffers. The mastering rack (0x3000) passes through; patches are not followed
 *     (every voice reaches the system output).
 * Parameter ids follow the structure order in shadPS4's ngs2.h / ngs2_sampler.h, confirmed by
 * the sizes the game sends. The game treats state flags != 0 as "still playing" (eboot 0x2d6ff1).
 * Interface and error codes as in shadPS4's src/core/libraries/ngs2 (GPL-2.0-or-later). */
#include "runtime.h"
#include "third_party/LibAtrac9/C/src/libatrac9.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERR_INVALID_HANDLE        ((int32_t)0x804A0200)
#define ERR_INVALID_BUFFER_INFO   ((int32_t)0x804A0206)
#define ERR_INVALID_SYSTEM_HANDLE ((int32_t)0x804A0230)
#define ERR_INVALID_RACK_HANDLE   ((int32_t)0x804A0261)
#define ERR_INVALID_VOICE_HANDLE  ((int32_t)0x804A0300)
#define ERR_INVALID_VOICE_INDEX   ((int32_t)0x804A0302)
#define ERR_INVALID_WAVEFORM      ((int32_t)0x804A0400)

/* Waveform types. FLOAT32 (0x18) is what the game's render buffers use; the other two are ours:
 * the game only passes back what ParseWaveformData returned. */
#define TYPE_PCM_I16   0x12u
#define TYPE_FLOAT32   0x18u
#define TYPE_ATRAC9    0x40u

/* Voice parameter ids (header.id). */
#define P_MATRIX_LEVELS   0x1u
#define P_PORT_MATRIX     0x2u
#define P_PORT_VOLUME     0x3u   /* sent as 0 at setup: not applied (see below) */
#define P_PATCH           0x5u
#define P_EVENT           0x6u
#define P_S_SETUP         0x10000000u
#define P_S_BLOCKS        0x10000001u
#define P_S_ADDRESS       0x10000002u
#define P_S_EXIT_LOOP     0x10000004u
#define P_S_PITCH         0x10000005u
enum { EV_PLAY = 0, EV_STOP = 1, EV_STOP_IMM = 2, EV_KILL = 3, EV_PAUSE = 4, EV_RESUME = 5 };
#define REPEAT_FOREVER 0xFFFFFFFFu

enum { KIND_SYSTEM = 0x4E475331, KIND_RACK = 0x4E475252, KIND_VOICE = 0x4E475656 };
enum { RACK_SAMPLER = 0x1000, RACK_MASTERING = 0x3000 };
#define MAX_VOICES   256
#define MAX_BLOCKS   64
#define MAX_MATRICES 4
#define MAX_CH       8

/* Guest layouts (shadPS4 ngs2.h). */
typedef struct { uint32_t type, channels, rate, config, frame_offset, frame_margin; } WaveformFormat;
typedef struct { uint32_t offset, size, repeats, skip, samples, reserved; uint64_t user; } WaveformBlock;
typedef struct {
    WaveformFormat format;
    uint32_t data_offset, data_size, loop_begin, loop_end, samples;
    uint32_t unit_size, unit_samples, units_per_frame, frame_size, frame_samples, delay_samples;
    uint32_t blocks;
    WaveformBlock block[4];
} WaveformInfo;
_Static_assert(sizeof(WaveformInfo) == 200, "SceNgs2WaveformInfo");
typedef struct { void *buffer; size_t size; uint32_t waveform_type, channels; } RenderBufferInfo;
typedef struct { size_t size; char name[16]; uint32_t flags, max_grain, max_voices; } RackOptionHead;

typedef struct {
    /* configuration */
    uint32_t type, channels, rate, config;
    const unsigned char *data;      /* the head block's */
    uint32_t blocks;                /* queued blocks, the head first */
    WaveformBlock block[MAX_BLOCKS];
    const unsigned char *block_data[MAX_BLOCKS];
    int stream, starving;           /* blocks are appended while it plays; waiting for one */
    uint64_t consumed;              /* bytes of finished blocks (decodedDataSize) */
    /* Streams: every appended block is copied here at once (its guest buffer is reused once
     * consumed), and decoded a whole superframe at a time: the game's 8 KiB pieces split them.
     * The first piece's skip and sample count hold for the whole stream. */
    unsigned char *fifo, *sf;
    uint32_t fifo_len, fifo_cap, sf_size;
    int stream_begun, left_set;
    /* kr-port: a stream is played in iterations: a piece with flags 0x1 starts one (its skip and
     * sample count, e.g. a looping BGM's loop end), 0x3 pieces continue it. The game sends the
     * whole file each time; after the count the rest is dropped and the next iteration follows
     * without the voice ending (it ends when none comes: a track without a loop). */
    uint64_t fifo_base;             /* stream bytes before the FIFO's head */
    struct { uint64_t at; uint32_t skip, left; } mark[4];
    uint32_t marks, drain_frames;
    int draining;
    float pitch;
    float level[MAX_MATRICES][MAX_CH * MAX_CH];
    uint32_t levels[MAX_MATRICES];
    uint32_t port_matrix;
    int playing, paused, exit_loop;
    /* ATRAC9 */
    void *at9;
    uint32_t at9_config;
    Atrac9CodecInfo info;
    /* position */
    uint32_t cur, repeats_done;
    uint32_t pos, end;              /* bytes from data */
    uint32_t sf_start, sf_frame;    /* superframe being decoded */
    uint32_t skip, left;            /* samples to drop; samples still to output (0: until end) */
    short pcm[256 * MAX_CH];
    uint32_t pcm_count, pcm_at;     /* frames in pcm, next one */
    float a[MAX_CH], b[MAX_CH], frac;
    int primed;
    uint64_t decoded;
} Sampler;

typedef struct Object { uint32_t kind; } Object;
typedef struct Voice { Object head; struct Rack *rack; uint32_t index; Sampler *s; } Voice;
typedef struct Rack {
    Object head;
    struct System *system;
    uint32_t id, voices;
    Voice voice[MAX_VOICES];
    struct Rack *next;
} Rack;
typedef struct System {
    Object head;
    uint32_t sample_rate, grain;
    Rack *racks;
    struct System *next;
} System;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static System *systems;
static size_t renders, controls, parses, plays, decode_errors, voices_mixed;
static float gain = -1;

static System *find_system(const void *h) {
    for (System *s = systems; s; s = s->next) if (s == h && s->head.kind == KIND_SYSTEM) return s;
    return NULL;
}
static Rack *find_rack(const void *h) {
    for (System *s = systems; s; s = s->next)
        for (Rack *r = s->racks; r; r = r->next) if (r == h && r->head.kind == KIND_RACK) return r;
    return NULL;
}
static Voice *find_voice(const void *h) {
    for (System *s = systems; s; s = s->next)
        for (Rack *r = s->racks; r; r = r->next)
            if ((const char *)h >= (const char *)r->voice && (const char *)h < (const char *)(r->voice + r->voices)
                && ((const Voice *)h)->head.kind == KIND_VOICE) return (Voice *)h;
    return NULL;
}
static int tracing(void) {
    static int trace = -1;
    if (trace < 0) { const char *e = getenv("KR_TRACE_NGS2"); trace = e && *e == '1'; }
    return trace;
}

/* ---------------------------------------------------------------- decoding ---- */
static void free_sampler(Sampler *s) {
    if (!s) return;
    if (s->at9) Atrac9ReleaseHandle(s->at9);
    free(s->fifo); free(s->sf);
    free(s);
}
static int start_block(Sampler *s, uint32_t i) {
    if (i >= s->blocks) return 0;
    const WaveformBlock *b = &s->block[i];
    s->cur = i;
    s->data = s->block_data[i];
    s->pos = s->sf_start = b->offset;
    s->end = b->offset + b->size;
    s->sf_frame = 0;
    s->skip = b->skip;
    s->left = b->samples;
    s->pcm_count = s->pcm_at = 0;
    return 1;
}
/* The block after the current one ends: its repeats, then the next block. */
static int next_block(Sampler *s) {
    const WaveformBlock *b = &s->block[s->cur];
    if (b->repeats == REPEAT_FOREVER ? !s->exit_loop : s->repeats_done < b->repeats) {
        ++s->repeats_done;
        return start_block(s, s->cur);
    }
    s->repeats_done = 0;
    s->consumed += b->size;
    if (!s->stream) return start_block(s, s->cur + 1);
    /* A stream: the finished block leaves the queue; with none left, wait for the next. */
    memmove(s->block, s->block + 1, (s->blocks - 1) * sizeof(*s->block));
    memmove(s->block_data, s->block_data + 1, (s->blocks - 1) * sizeof(*s->block_data));
    if (--s->blocks) return start_block(s, 0);
    s->starving = 1;
    return 0;
}
/* kr-port: LibAtrac9 keeps its place in the superframe (IndexInSuperframe) between calls. A
 * sound that stopped mid-superframe, or a rejected frame, left the next sound misaligned: every
 * frame failed after it (silent music) and corrupt frames could crash the decoder. The decoder
 * starts over with every sound, stream and error (Atrac9ResetDecoder: InitDecoder kept that place). */
static void stream_reset(Sampler *s);
static void at9_restart(Sampler *s) {
    if (!s->at9) return;
    if (Atrac9ResetDecoder(s->at9) != 0) { Atrac9ReleaseHandle(s->at9); s->at9 = NULL; s->at9_config = 0; }
}
/* kr-port: decode errors are logged (the first 30): a corrupt or misaligned ATRAC9 frame. */
static void decode_error(Sampler *s, int rc, int stream) {
    at9_restart(s);
    if (++decode_errors <= 30)
        printf("Runtime: Ngs2 ATRAC9 decode error %d (%s, config %08x, superframe %d B, pos %u, frame %u, fifo %u)\n",
               rc, stream ? "stream" : "block", s->config, s->info.superframeSize, s->pos, s->sf_frame, s->fifo_len);
}
static void fifo_drop(Sampler *s, uint32_t n) {
    if (n > s->fifo_len) n = s->fifo_len;
    memmove(s->fifo, s->fifo + n, s->fifo_len - n);
    s->fifo_len -= n; s->fifo_base += n; s->consumed += n;
}
static void iteration_begin(Sampler *s, uint32_t skip, uint32_t left) {
    s->skip = skip; s->left = left; s->left_set = left != 0;
    s->draining = 0; s->drain_frames = 0;
    s->sf_frame = 0; s->pcm_count = s->pcm_at = 0;
    at9_restart(s);
}
/* One codec frame into s->pcm. 0 at the end of the block's data. */
static int decode_stream_frame(Sampler *s) {
    const uint32_t sf_bytes = (uint32_t)s->info.superframeSize;
    if (!s->at9 || !sf_bytes) return 0;
    while (s->sf_frame == 0) {                   /* iteration boundaries and the dropped rest */
        const uint64_t to_mark = s->marks ? s->mark[0].at - s->fifo_base : UINT64_MAX;
        if (s->marks && (s->draining || to_mark < sf_bytes)) {
            if (to_mark > s->fifo_len) { fifo_drop(s, s->fifo_len); s->starving = 1; return 0; }
            fifo_drop(s, (uint32_t)to_mark);
            iteration_begin(s, s->mark[0].skip, s->mark[0].left);
            memmove(s->mark, s->mark + 1, (--s->marks) * sizeof(*s->mark));
            continue;
        }
        if (s->draining) { fifo_drop(s, s->fifo_len); s->starving = 1; return 0; }
        break;
    }
    if (s->sf_frame == 0) {                      /* a new superframe from the FIFO */
        if (s->fifo_len < sf_bytes) { s->starving = 1; return 0; }
        if (s->sf_size < sf_bytes) {
            unsigned char *n = realloc(s->sf, sf_bytes + 64);
            if (!n) return 0;
            s->sf = n; s->sf_size = sf_bytes;
        }
        memcpy(s->sf, s->fifo, sf_bytes);
        memset(s->sf + sf_bytes, 0, 64);
        fifo_drop(s, sf_bytes);
        s->pos = 0;
    }
    int used = 0;
    int rc = Atrac9Decode(s->at9, s->sf + s->pos, s->pcm, &used);
    if (rc != 0 || used <= 0) { decode_error(s, rc, 1); s->sf_frame = 0; return 0; }
    s->pos += (uint32_t)used;
    if (++s->sf_frame >= (uint32_t)s->info.framesInSuperframe) s->sf_frame = 0;
    s->pcm_count = (uint32_t)s->info.frameSamples;
    s->pcm_at = 0;
    return 1;
}
static int decode_frame(Sampler *s) {
    if (s->stream) return decode_stream_frame(s);
    if (s->type == TYPE_ATRAC9) {
        if (!s->at9 || s->pos >= s->end) return 0;
        int used = 0;
        int rc = Atrac9Decode(s->at9, s->data + s->pos, s->pcm, &used);
        if (rc != 0 || used <= 0) { decode_error(s, rc, 0); return 0; }
        s->pos += (uint32_t)used;
        if (++s->sf_frame >= (uint32_t)s->info.framesInSuperframe) {   /* next superframe, aligned */
            s->sf_start += (uint32_t)s->info.superframeSize;
            s->pos = s->sf_start;
            s->sf_frame = 0;
        }
        s->pcm_count = (uint32_t)s->info.frameSamples;
    } else if (s->type == TYPE_PCM_I16) {
        uint32_t frame = 2 * s->channels, n = (s->end - s->pos) / frame;
        if (!n) return 0;
        if (n > 256) n = 256;
        memcpy(s->pcm, s->data + s->pos, (size_t)n * frame);
        s->pos += n * frame;
        s->pcm_count = n;
    } else return 0;
    s->pcm_at = 0;
    return 1;
}
/* The next sample frame of the voice, as floats. 0 when the voice has ended. */
static int fetch(Sampler *s, float *out) {
    for (;;) {
        if (s->stream && s->left_set && !s->left && !s->draining) {   /* the iteration's count is out */
            s->draining = 1; s->drain_frames = 0; s->pcm_count = s->pcm_at = 0;
        }
        if (s->pcm_at < s->pcm_count) {
            const short *p = s->pcm + (size_t)s->pcm_at++ * s->channels;
            if (s->skip) { --s->skip; continue; }
            for (uint32_t c = 0; c < s->channels; ++c) out[c] = p[c] * (1.0f / 32768.0f);
            ++s->decoded;
            if (s->stream) { if (s->left) --s->left; }
            else if (s->left && !--s->left) s->pcm_count = s->pcm_at = 0, s->pos = s->end;   /* block done */
            return 1;
        }
        if (s->stream) {
            s->starving = 0;
            if (decode_frame(s)) continue;
            return s->starving ? -1 : 0;
        }
        if (decode_frame(s)) continue;
        if (!next_block(s)) return 0;
    }
}
static void play(Sampler *s) {
    /* Played without blocks: a stream, whose blocks come while it plays. */
    if (!s->blocks) { s->stream = 1; stream_reset(s); }
    s->playing = (s->blocks > 0 || s->stream) && (s->type == TYPE_PCM_I16 || (s->type == TYPE_ATRAC9 && s->at9));
    s->starving = 0;
    s->consumed = 0;
    s->paused = 0;
    s->repeats_done = 0;
    s->primed = 0;
    s->decoded = 0;
    if (s->type == TYPE_ATRAC9) at9_restart(s);
    if (s->playing) { if (s->blocks && !s->stream) start_block(s, 0); ++plays; }
    if (tracing())
        printf("NGS2 play: type=%#x ch=%u rate=%u blocks=%u [%u+%u skip %u n %u rep %#x] data=%p -> %s\n",
               s->type, s->channels, s->rate, s->blocks, s->blocks ? s->block[0].offset : 0, s->blocks ? s->block[0].size : 0,
               s->blocks ? s->block[0].skip : 0, s->blocks ? s->block[0].samples : 0, s->blocks ? s->block[0].repeats : 0,
               (const void *)s->data, s->playing ? "playing" : "NOT playing");
}
/* A finished iteration with no next one for half a second: the track had no loop, the voice ends. */
static void drain_wait(Sampler *s, uint32_t frames) {
    if (!s->draining || s->marks) return;
    s->drain_frames += frames;
    if (s->drain_frames >= (s->rate ? s->rate : 48000) / 2) {
        s->playing = 0;
        if (tracing()) printf("NGS2 stream ended after %llu samples\n", (unsigned long long)s->decoded);
    }
}
/* Adds `frames` frames of the voice into out (out_ch interleaved floats). */
static void mix_voice(Sampler *s, float *out, uint32_t frames, uint32_t out_ch, uint32_t system_rate) {
    if (!s->primed) {
        memset(s->a, 0, sizeof(s->a)); memset(s->b, 0, sizeof(s->b));
        int r = fetch(s, s->a);
        if (r > 0) r = fetch(s, s->b);
        if (r < 0) { drain_wait(s, frames); return; }   /* a stream waiting for data: silence */
        if (!r) { s->playing = 0; return; }
        s->frac = 0; s->primed = 1;
    }
    const uint32_t in_ch = s->channels;
    const uint32_t m = s->port_matrix < MAX_MATRICES ? s->port_matrix : 0;
    const float *lv = s->level[m];
    uint32_t nl = s->levels[m];
    static const float mono_default[2] = {0.7071f, 0.7071f}, stereo_default[4] = {1, 0, 0, 1};
    if (!nl) { lv = in_ch == 1 ? mono_default : stereo_default; nl = in_ch == 1 ? 2 : 4; }
    const uint32_t matrix_out = nl / in_ch;
    const uint32_t outs = matrix_out < out_ch ? matrix_out : out_ch;
    const float step = s->pitch * (float)s->rate / (float)(system_rate ? system_rate : 48000);
    for (uint32_t f = 0; f < frames; ++f) {
        while (s->frac >= 1.0f) {
            float next[MAX_CH];
            int r = fetch(s, next);
            if (r < 0) { drain_wait(s, frames); return; }   /* starving: the rest of the grain is silence */
            memcpy(s->a, s->b, sizeof(s->a));
            memcpy(s->b, next, sizeof(s->b));
            if (!r) {
                s->playing = 0;
                if (tracing()) printf("NGS2 voice ended after %llu samples (block %u, pos %u/%u)\n",
                                      (unsigned long long)s->decoded, s->cur, s->pos, s->end);
                return;
            }
            s->frac -= 1.0f;
        }
        float v[MAX_CH];
        for (uint32_t c = 0; c < in_ch; ++c) v[c] = s->a[c] + (s->b[c] - s->a[c]) * s->frac;
        float *o = out + (size_t)f * out_ch;
        for (uint32_t oc = 0; oc < outs; ++oc) {
            float acc = 0;
            for (uint32_t ic = 0; ic < in_ch; ++ic) acc += v[ic] * lv[oc * in_ch + ic];
            o[oc] += acc * gain;
        }
        s->frac += step;
    }
}

/* ---------------------------------------------------------------- objects ---- */
static ABI int32_t system_create(const void *option, const void *allocator, void **out) {
    (void)option; (void)allocator;   /* the host allocates; the game's allocator is not needed */
    if (!out) return ERR_INVALID_HANDLE;
    System *s = calloc(1, sizeof(*s));
    if (!s) return ERR_INVALID_HANDLE;
    s->head.kind = KIND_SYSTEM; s->sample_rate = 48000; s->grain = 256;
    pthread_mutex_lock(&lock);
    if (gain < 0) { const char *e = getenv("KR_NGS2_GAIN"); gain = e && *e ? (float)atof(e) : 1.0f; }
    s->next = systems; systems = s;
    pthread_mutex_unlock(&lock);
    *out = s;
    printf("Runtime: Ngs2 system created (ATRAC9 sampler mixer, gain %.2f)\n", gain);
    return 0;
}
static ABI int32_t rack_create(void *system, uint32_t rack_id, const RackOptionHead *option,
                               const void *allocator, void **out) {
    (void)allocator;
    if (!out) return ERR_INVALID_HANDLE;
    pthread_mutex_lock(&lock);
    System *s = find_system(system);
    if (!s) { pthread_mutex_unlock(&lock); return ERR_INVALID_SYSTEM_HANDLE; }
    Rack *r = calloc(1, sizeof(*r));
    if (!r) { pthread_mutex_unlock(&lock); return ERR_INVALID_HANDLE; }
    r->head.kind = KIND_RACK; r->system = s; r->id = rack_id;
    uint32_t voices = option && option->size >= sizeof(*option) && option->max_voices ? option->max_voices : 1;
    r->voices = voices > MAX_VOICES ? MAX_VOICES : voices;
    for (uint32_t i = 0; i < r->voices; ++i) { r->voice[i].head.kind = KIND_VOICE; r->voice[i].rack = r; r->voice[i].index = i; }
    r->next = s->racks; s->racks = r;
    pthread_mutex_unlock(&lock);
    *out = r;
    printf("Runtime: Ngs2 rack id=%#x with %u voices\n", rack_id, r->voices);
    return 0;
}
static ABI int32_t rack_get_voice(void *rack, uint32_t index, void **out) {
    if (!out) return ERR_INVALID_HANDLE;
    pthread_mutex_lock(&lock);
    Rack *r = find_rack(rack);
    int32_t e = !r ? ERR_INVALID_RACK_HANDLE : index >= r->voices ? ERR_INVALID_VOICE_INDEX : 0;
    if (!e) *out = &r->voice[index];
    pthread_mutex_unlock(&lock);
    return e;
}
static void free_rack(Rack *r) {
    for (uint32_t i = 0; i < r->voices; ++i) free_sampler(r->voice[i].s);
    r->head.kind = 0; free(r);
}
static ABI int32_t rack_destroy(void *rack, void *out_buffer) {
    (void)out_buffer;
    pthread_mutex_lock(&lock);
    Rack *r = find_rack(rack);
    if (!r) { pthread_mutex_unlock(&lock); return ERR_INVALID_RACK_HANDLE; }
    for (Rack **p = &r->system->racks; *p; p = &(*p)->next) if (*p == r) { *p = r->next; break; }
    free_rack(r);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t system_destroy(void *system, void *out_buffer) {
    (void)out_buffer;
    pthread_mutex_lock(&lock);
    System *s = find_system(system);
    if (!s) { pthread_mutex_unlock(&lock); return ERR_INVALID_SYSTEM_HANDLE; }
    for (System **p = &systems; *p; p = &(*p)->next) if (*p == s) { *p = s->next; break; }
    while (s->racks) { Rack *r = s->racks; s->racks = r->next; free_rack(r); }
    s->head.kind = 0; free(s);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t system_set_sample_rate(void *system, uint32_t rate) {
    pthread_mutex_lock(&lock);
    System *s = find_system(system);
    if (s) s->sample_rate = rate;
    pthread_mutex_unlock(&lock);
    return s ? 0 : ERR_INVALID_SYSTEM_HANDLE;
}
static ABI int32_t system_set_grain(void *system, uint32_t samples) {
    pthread_mutex_lock(&lock);
    System *s = find_system(system);
    if (s) s->grain = samples;
    pthread_mutex_unlock(&lock);
    return s ? 0 : ERR_INVALID_SYSTEM_HANDLE;
}
static ABI int32_t system_render(void *system, const RenderBufferInfo *info, uint32_t count) {
    if (count && !info) return ERR_INVALID_BUFFER_INFO;
    /* KR_NGS2_PROF=1: the render's cost (lock wait and mix) and voices, every 5 s. */
    static int prof = -1;
    if (prof < 0) { const char *e = getenv("KR_NGS2_PROF"); prof = e && e[0] == '1'; }
    struct timespec ts0, ts1, ts2;
    if (prof) clock_gettime(CLOCK_MONOTONIC, &ts0);
    pthread_mutex_lock(&lock);
    if (prof) clock_gettime(CLOCK_MONOTONIC, &ts1);
    const size_t mixed_before = voices_mixed;
    System *sys = find_system(system);
    if (!sys) { pthread_mutex_unlock(&lock); return ERR_INVALID_SYSTEM_HANDLE; }
    ++renders;
    static int traced;
    if (!traced && tracing()) {
        traced = 1;
        for (uint32_t i = 0; i < count; ++i)
            printf("NGS2 render buffer %u: size=%zu type=%#x channels=%u\n", i, info[i].size, info[i].waveform_type, info[i].channels);
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!info[i].buffer || !info[i].size) continue;
        memset(info[i].buffer, 0, info[i].size);
        if (info[i].waveform_type != TYPE_FLOAT32 || !info[i].channels || info[i].channels > MAX_CH) continue;
        const uint32_t ch = info[i].channels;
        const uint32_t frames = (uint32_t)(info[i].size / (sizeof(float) * ch));
        for (Rack *r = sys->racks; r; r = r->next) {
            if (r->id != RACK_SAMPLER) continue;
            for (uint32_t v = 0; v < r->voices; ++v) {
                Sampler *s = r->voice[v].s;
                if (!s || !s->playing || s->paused) continue;
                mix_voice(s, info[i].buffer, frames, ch, sys->sample_rate);
                ++voices_mixed;
            }
        }
        /* The PS4's mastering rack limits the mix; here a soft knee above 0.8 keeps busy scenes
         * (many effects at once reached 1.44) from clipping. */
        float *o = info[i].buffer;
        for (size_t k = 0, n = (size_t)frames * ch; k < n; ++k) {
            float x = o[k], m = fabsf(x);
            if (m > 0.8f) o[k] = copysignf(0.8f + 0.2f * tanhf((m - 0.8f) / 0.2f), x);
        }
    }
    if (prof) {
        clock_gettime(CLOCK_MONOTONIC, &ts2);
        static double wait_sum, mix_sum, mix_max, t_start; static size_t n, voices, voices_max;
        const double wait = (ts1.tv_sec - ts0.tv_sec) * 1e3 + (ts1.tv_nsec - ts0.tv_nsec) / 1e6;
        const double mix = (ts2.tv_sec - ts1.tv_sec) * 1e3 + (ts2.tv_nsec - ts1.tv_nsec) / 1e6;
        const size_t vm = voices_mixed - mixed_before;
        const double now = ts2.tv_sec + ts2.tv_nsec / 1e9;
        if (!t_start) t_start = now;
        wait_sum += wait; mix_sum += mix; if (mix > mix_max) mix_max = mix;
        voices += vm; if (vm > voices_max) voices_max = vm; ++n;
        if (now - t_start >= 5.0) {
            printf("KR ngs2 prof: %zu renders, mix avg %.3f ms max %.3f ms, lock wait avg %.3f ms, voices avg %.1f max %zu\n",
                   n, mix_sum / n, mix_max, wait_sum / n, (double)voices / n, voices_max);
            wait_sum = mix_sum = mix_max = 0; n = voices = voices_max = 0; t_start = now;
        }
    }
    pthread_mutex_unlock(&lock);
    return 0;
}

/* ---------------------------------------------------------------- voice control ---- */
/* KR_TRACE_NGS2=1: every parameter of the first 400 (id, size, payload words). */
static void trace_param(const Voice *v, const unsigned char *p, uint16_t size, uint32_t id) {
    static int lines;
    if (!tracing() || lines++ >= 400) return;
    uint32_t w[6] = {0};
    memcpy(w, p + 8, size >= 8 + sizeof(w) ? sizeof(w) : size > 8 ? size - 8u : 0u);
    printf("NGS2 rack %#x voice %u param id=%#x size=%u: %08x %08x %08x %08x %08x %08x\n",
           v->rack->id, v->index, id, size, w[0], w[1], w[2], w[3], w[4], w[5]);
}
static void setup_atrac9(Sampler *s) {
    if (s->at9 && s->at9_config == s->config) { at9_restart(s); return; }
    if (!s->at9) s->at9 = Atrac9GetHandle();
    unsigned char config[4]; memcpy(config, &s->config, 4);
    if (!s->at9 || Atrac9InitDecoder(s->at9, config) != 0 || Atrac9GetCodecInfo(s->at9, &s->info) != 0 ||
        s->info.channels != (int)s->channels || s->info.frameSamples > 256) {
        fprintf(stderr, "Runtime: Ngs2 ATRAC9 config %08x not decodable\n", s->config);
        if (s->at9) Atrac9ReleaseHandle(s->at9);
        s->at9 = NULL; s->at9_config = 0;
        return;
    }
    s->at9_config = s->config;
}
/* kr-port: a stopped stream's leftovers (a partial superframe in the FIFO, its skip and count)
 * must not carry into the next sound the voice plays: they misaligned it. */
static void stream_reset(Sampler *s) {
    if (!s->stream) return;
    s->fifo_len = 0; s->stream_begun = s->left_set = 0; s->sf_frame = 0; s->pos = 0;
    s->pcm_count = s->pcm_at = 0; s->skip = s->left = 0; s->starving = 0;
    s->fifo_base = 0; s->marks = 0; s->draining = 0; s->drain_frames = 0;
}
static void apply(Voice *v, const unsigned char *p, uint32_t id) {
    if (v->rack->id != RACK_SAMPLER) return;   /* mastering: passes through */
    if (!v->s && !(v->s = calloc(1, sizeof(Sampler)))) return;
    Sampler *s = v->s;
    switch (id) {
    case P_S_SETUP: {
        WaveformFormat f; memcpy(&f, p + 8, sizeof(f));
        s->playing = s->paused = s->exit_loop = 0;
        s->type = f.type; s->channels = f.channels; s->rate = f.rate; s->config = f.config;
        s->blocks = 0; s->data = NULL; s->pitch = 1.0f; s->port_matrix = 0;
        s->stream = s->starving = 0; s->consumed = 0;
        s->fifo_len = 0; s->stream_begun = s->left_set = 0; s->sf_frame = 0; s->pcm_count = s->pcm_at = 0;
        s->skip = s->left = 0;
        if (s->channels == 0 || s->channels > MAX_CH) s->channels = 0, s->type = 0;
        if (s->type == TYPE_ATRAC9) setup_atrac9(s);
        break;
    }
    case P_S_BLOCKS: {
        const unsigned char *data; const WaveformBlock *blocks; uint32_t flags, n;
        memcpy(&data, p + 8, 8); memcpy(&flags, p + 16, 4); memcpy(&n, p + 20, 4); memcpy(&blocks, p + 24, 8);
        static int traced;
        if (tracing() && traced++ < 4000)
            printf("NGS2 blocks: flags=%#x n=%u data=%p [%u+%u skip %u n %u rep %#x] queued=%u playing=%d\n", flags, n,
                   (const void *)data, blocks && n ? blocks[0].offset : 0, blocks && n ? blocks[0].size : 0,
                   blocks && n ? blocks[0].skip : 0, blocks && n ? blocks[0].samples : 0, blocks && n ? blocks[0].repeats : 0,
                   s->blocks, s->playing);
        if (flags) s->stream = 1;                /* appended to a stream */
        if (s->stream) {
            for (uint32_t i = 0; blocks && data && i < n; ++i) {
                const WaveformBlock *b = &blocks[i];
                if (!s->stream_begun) {
                    s->stream_begun = 1;
                    iteration_begin(s, b->skip, b->samples);
                } else if ((flags & 3) == 1 && i == 0 && s->marks < 4) {   /* the next iteration */
                    s->mark[s->marks].at = s->fifo_base + s->fifo_len;
                    s->mark[s->marks].skip = b->skip; s->mark[s->marks].left = b->samples;
                    ++s->marks;
                }
                if (s->fifo_len + b->size > s->fifo_cap) {
                    uint32_t cap = s->fifo_cap ? s->fifo_cap : 65536;
                    while (cap < s->fifo_len + b->size) cap *= 2;
                    unsigned char *f = cap <= (64u << 20) ? realloc(s->fifo, cap) : NULL;
                    if (!f) { ++decode_errors; break; }
                    s->fifo = f; s->fifo_cap = cap;
                }
                memcpy(s->fifo + s->fifo_len, data + b->offset, b->size);
                s->fifo_len += b->size;
            }
            s->data = data;
            break;
        }
        s->blocks = 0;                           /* replaces the waveform */
        for (uint32_t i = 0; blocks && i < n && s->blocks < MAX_BLOCKS; ++i) {
            s->block[s->blocks] = blocks[i];
            s->block_data[s->blocks++] = data;
        }
        if (s->blocks) s->data = s->block_data[0];
        break;
    }
    case P_S_ADDRESS: {
        const unsigned char *from, *to; memcpy(&from, p + 8, 8); memcpy(&to, p + 16, 8);
        for (uint32_t i = 0; i < s->blocks; ++i) if (s->block_data[i] == from) s->block_data[i] = to;
        if (s->data == from) s->data = to;
        break;
    }
    case P_S_EXIT_LOOP: s->exit_loop = 1; break;
    case P_S_PITCH: { float r; memcpy(&r, p + 8, 4); s->pitch = r > 0.01f && r < 16.0f ? r : 1.0f; break; }
    case P_MATRIX_LEVELS: {
        uint32_t m, n; const float *lv;
        memcpy(&m, p + 8, 4); memcpy(&n, p + 12, 4); memcpy(&lv, p + 16, 8);
        if (m < MAX_MATRICES && lv && n <= MAX_CH * MAX_CH) { memcpy(s->level[m], lv, n * sizeof(float)); s->levels[m] = n; }
        break;
    }
    case P_PORT_MATRIX: { int32_t m; memcpy(&m, p + 12, 4); s->port_matrix = m >= 0 && m < MAX_MATRICES ? (uint32_t)m : 0; break; }
    case P_PORT_VOLUME: break;   /* the game sends level 0 with every setup and never another: not a gain */
    case P_PATCH: break;
    case P_EVENT: {
        uint32_t e; memcpy(&e, p + 8, 4);
        switch (e) {
        case EV_PLAY: play(s); break;
        case EV_STOP: case EV_STOP_IMM: s->playing = 0; stream_reset(s); break;
        case EV_KILL: s->playing = 0; s->blocks = 0; stream_reset(s); break;
        case EV_PAUSE: s->paused = 1; break;
        case EV_RESUME: s->paused = 0; break;
        default: { static int seen; if (!seen++) printf("Runtime: Ngs2 voice event %u ignored\n", e); }
        }
        break;
    }
    default: {
        static uint32_t seen[16]; static int n;
        int known = 0;
        for (int i = 0; i < n; ++i) known |= seen[i] == id;
        if (!known && n < 16) { seen[n++] = id; printf("Runtime: Ngs2 voice parameter %#x ignored\n", id); }
    }
    }
}
static ABI int32_t voice_control(void *voice, const void *params) {
    pthread_mutex_lock(&lock);
    Voice *v = find_voice(voice);
    if (v) {
        ++controls;
        const unsigned char *p = params;
        for (int n = 0; p && n < 64; ++n) {
            uint16_t size; int16_t next; uint32_t id;
            memcpy(&size, p, 2); memcpy(&next, p + 2, 2); memcpy(&id, p + 4, 4);
            trace_param(v, p, size, id);
            apply(v, p, id);
            if (!next) break;
            p += next;
        }
    }
    pthread_mutex_unlock(&lock);
    return v ? 0 : ERR_INVALID_VOICE_HANDLE;
}
static uint32_t voice_flags(const Voice *v) {
    const Sampler *s = v->s;
    if (!s || !(s->playing || s->paused)) return 0;
    return 1u | (s->paused ? 4u : 2u);   /* in use | paused or playing */
}
static ABI int32_t voice_get_state(void *voice, void *state, size_t size) {
    pthread_mutex_lock(&lock);
    Voice *v = find_voice(voice);
    if (v && state && size) {
        unsigned char out[48] = {0};
        uint32_t flags = voice_flags(v);
        float one = 1.0f;
        memcpy(out, &flags, 4);
        memcpy(out + 4, &one, 4);
        if (v->s) {
            const Sampler *s = v->s;
            uint64_t bytes = s->consumed;
            if (!s->stream && s->blocks && s->pos > s->block[s->cur].offset) bytes += s->pos - s->block[s->cur].offset;
            memcpy(out + 16, &s->decoded, 8); memcpy(out + 24, &bytes, 8); memcpy(out + 40, &s->data, 8);
        }
        memcpy(state, out, size < sizeof(out) ? size : sizeof(out));
        if (size > sizeof(out)) memset((char *)state + sizeof(out), 0, size - sizeof(out));
    }
    pthread_mutex_unlock(&lock);
    return v ? 0 : ERR_INVALID_VOICE_HANDLE;
}
static ABI int32_t voice_get_state_flags(void *voice, uint32_t *flags) {
    pthread_mutex_lock(&lock);
    Voice *v = find_voice(voice);
    if (v && flags) *flags = voice_flags(v);
    pthread_mutex_unlock(&lock);
    return v ? 0 : ERR_INVALID_VOICE_HANDLE;
}

/* ---------------------------------------------------------------- waveform ---- */
static uint32_t rd32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const unsigned char *p) { uint16_t v; memcpy(&v, p, 2); return v; }
/* RIFF/WAVE: ATRAC9 (WAVE_FORMAT_EXTENSIBLE with the ATRAC9 subformat) or 16-bit PCM. Blocks:
 * one for the whole stream; with a smpl loop, the intro up to the loop end and then the loop,
 * repeated until the voice gets "exit loop". Loop points count after the encoder delay. */
static ABI int32_t parse_waveform(const void *data, size_t size, WaveformInfo *out) {
    static const unsigned char at9_guid[16] = {0xd2,0x42,0xe1,0x47,0xba,0x36,0x8d,0x4d,0x88,0xfc,0x61,0x65,0x4f,0x8c,0x83,0x6c};
    const unsigned char *p = data;
    if (!p || !out || size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return ERR_INVALID_WAVEFORM;
    memset(out, 0, sizeof(*out));
    uint32_t tag = 0, align = 0, bits = 0, unit_samples = 0, total = 0, delay = 0, loops = 0, lb = 0, le = 0, config = 0;
    int have_data = 0;
    for (size_t at = 12; at + 8 <= size;) {
        uint32_t length = rd32(p + at + 4);
        const unsigned char *body = p + at + 8;
        size_t avail = size - (at + 8);
        if (!memcmp(p + at, "fmt ", 4) && length >= 16 && avail >= 16) {
            tag = rd16(body); out->format.channels = rd16(body + 2); out->format.rate = rd32(body + 4);
            align = rd16(body + 12); bits = rd16(body + 14);
            if (tag == 0xFFFE && length >= 48 && avail >= 48 && !memcmp(body + 24, at9_guid, 16)) {
                unit_samples = rd16(body + 18);
                config = rd32(body + 44);
            }
        } else if (!memcmp(p + at, "fact", 4) && length >= 8 && avail >= 8) {
            total = rd32(body);
            delay = length >= 12 && avail >= 12 ? rd32(body + 8) : rd32(body + 4);
        } else if (!memcmp(p + at, "smpl", 4) && length >= 60 && avail >= 60) {
            loops = rd32(body + 28);
            lb = rd32(body + 44); le = rd32(body + 48);
        } else if (!memcmp(p + at, "data", 4)) {
            out->data_offset = (uint32_t)(at + 8); out->data_size = length; have_data = 1;
        }
        at += 8 + (size_t)length + (length & 1);
    }
    uint32_t ch = out->format.channels;
    if (!have_data || !ch || ch > MAX_CH) return ERR_INVALID_WAVEFORM;
    if (config && align && unit_samples) {
        out->format.type = TYPE_ATRAC9;
        out->format.config = config;
        out->unit_size = align; out->unit_samples = unit_samples; out->units_per_frame = 1;
        out->frame_size = align; out->frame_samples = unit_samples;
    } else if (tag == 1 && bits == 16) {
        out->format.type = TYPE_PCM_I16;
        delay = 0;
        if (!total) total = out->data_size / (2 * ch);
        out->unit_size = out->frame_size = 2 * ch; out->unit_samples = out->frame_samples = 1; out->units_per_frame = 1;
    } else return ERR_INVALID_WAVEFORM;
    out->samples = total;
    out->delay_samples = delay;
    WaveformBlock *b = out->block;
    b[0] = (WaveformBlock){out->data_offset, out->data_size, 0, delay, total, 0, 0};
    out->blocks = 1;
    if (loops && le > lb && le < total) {
        out->loop_begin = lb; out->loop_end = le;
        b[0].samples = le + 1;
        uint32_t start = lb + delay, unit = out->unit_samples, first = start / unit;
        b[1] = (WaveformBlock){out->data_offset + first * out->unit_size, out->data_size - first * out->unit_size,
                               REPEAT_FOREVER, start - first * unit, le - lb + 1, 0, 0};
        out->blocks = 2;
    }
    pthread_mutex_lock(&lock); ++parses; pthread_mutex_unlock(&lock);
    return 0;
}

uintptr_t runtime_ngs2_resolve(const char *name) {
    if (runtime_nid_is(name, "mPYgU4oYpuY")) return (uintptr_t)system_create;
    if (runtime_nid_is(name, "u-WrYDaJA3k")) return (uintptr_t)system_destroy;
    if (runtime_nid_is(name, "U546k6orxQo")) return (uintptr_t)rack_create;
    if (runtime_nid_is(name, "lCqD7oycmIM")) return (uintptr_t)rack_destroy;
    if (runtime_nid_is(name, "MwmHz8pAdAo")) return (uintptr_t)rack_get_voice;
    if (runtime_nid_is(name, "i0VnXM-C9fc")) return (uintptr_t)system_render;
    if (runtime_nid_is(name, "-tbc2SxQD60")) return (uintptr_t)system_set_sample_rate;
    if (runtime_nid_is(name, "l4Q2dWEH6UM")) return (uintptr_t)system_set_grain;
    if (runtime_nid_is(name, "uu94irFOGpA")) return (uintptr_t)voice_control;
    if (runtime_nid_is(name, "-TOuuAQ-buE")) return (uintptr_t)voice_get_state;
    if (runtime_nid_is(name, "rEh728kXk3w")) return (uintptr_t)voice_get_state_flags;
    if (runtime_nid_is(name, "hyVLT2VlOYk")) return (uintptr_t)parse_waveform;
    return 0;
}
void runtime_ngs2_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: Ngs2 renders=%zu, voice controls=%zu, waveforms parsed=%zu, plays=%zu, voice mixes=%zu, ATRAC9 errors=%zu\n",
           renders, controls, parses, plays, voices_mixed, decode_errors);
    pthread_mutex_unlock(&lock);
}
