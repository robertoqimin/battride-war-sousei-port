// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: libkernel functions this game (or its own libc.prx) needs and bbport does not have.
 *   sceKernelConvertUtcToLocaltime / sceKernelConvertLocaltimeToUtc: the host time zone,
 *   from localtime_r's tm_gmtoff (which already includes daylight saving time). */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <time.h>

#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))

typedef struct { int64_t t; uint32_t west_sec, dst_sec; } Timesec;         /* OrbisTimesec */
typedef struct { int32_t minutes_west, dst_minutes; } KernelTimezone;      /* OrbisKernelTimezone */

static void offsets(time_t at, int32_t *standard, int32_t *dst) {
    struct tm local;
    if (!localtime_r(&at, &local)) { *standard = 0; *dst = 0; return; }
    *dst = local.tm_isdst > 0 ? 3600 : 0;
    *standard = (int32_t)local.tm_gmtoff - *dst;   /* seconds east of UTC, without DST */
}
/* sceKernelConvertUtcToLocaltime(time, *local, *timesec, *dst_seconds) */
static ABI int32_t utc_to_local(int64_t utc, int64_t *local, Timesec *st, uint64_t *dst_out) {
    int32_t standard, dst;
    offsets((time_t)utc, &standard, &dst);
    if (local) *local = utc + standard + dst;
    if (st) { st->t = utc; st->west_sec = (uint32_t)standard; st->dst_sec = (uint32_t)dst; }
    if (dst_out) *dst_out = (uint64_t)dst;
    return 0;
}
/* sceKernelConvertLocaltimeToUtc(local, reserved, *utc, *timezone, *dst_seconds) */
static ABI int32_t local_to_utc(int64_t local, int64_t reserved, int64_t *utc, KernelTimezone *tz,
                                int32_t *dst_out) {
    (void)reserved;
    if (!tz) return ERR(22);
    int32_t standard, dst;
    offsets((time_t)local, &standard, &dst);   /* the offset at that moment is close enough */
    tz->minutes_west = -standard / 60;
    tz->dst_minutes = dst / 60;
    if (utc) *utc = local - standard - dst;
    if (dst_out) *dst_out = dst;
    return 0;
}

uintptr_t runtime_krkernel_resolve(const char *name) {
    if (runtime_nid_is(name, "-o5uEDpN+oY")) return (uintptr_t)utc_to_local;
    if (runtime_nid_is(name, "0NTHN1NKONI")) return (uintptr_t)local_to_utc;
    return 0;
}
