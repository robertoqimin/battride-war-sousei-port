// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: what the game's own libc.prx asks the kernel while it unwinds a C++ exception (throw ->
 * catch, destructors on the way). Its unwinder finds the unwind tables of the module a return
 * address belongs to through these; without them the first exception stopped the game at an
 * unsupported import.
 *   sceKernelGetModuleInfoForUnwind / sceKernelGetModuleInfoFromAddr: the module containing an
 *     address, its .eh_frame_hdr (the sorted index of .eh_frame) and .eh_frame, its code segment.
 *   __elf_phdr_match_addr: FreeBSD libc's test of an address against a module's executable
 *     PT_LOAD segments (dl_iterate_phdr callbacks).
 *   _is_signal_return: whether a frame is the signal trampoline's (never here: guest signal
 *     handlers are not entered through a guest trampoline).
 *   sigreturn: not used by unwinding itself; reported and refused.
 * The modules are the regions of the linked image the loader registers (the eboot below the first
 * linked module, then each module). Each region's .eh_frame_hdr is found once, on first use, by
 * its header (version 1, eh_frame_ptr pcrel|sdata4, fde_count udata4, table datarel|sdata4) and
 * checked against its table; the layouts follow shadPS4's (OrbisModuleInfoForUnwind,
 * OrbisKernelModuleInfoEx). */
#define _GNU_SOURCE
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define E_SRCH 3
#define E_FAULT 14
#define E_INVAL 22

typedef struct {
    uint64_t st_size;
    char name[256];
    uint64_t eh_frame_hdr_addr, eh_frame_addr, eh_frame_size, seg0_addr, seg0_size;
} UnwindInfo;   /* OrbisModuleInfoForUnwind */
typedef struct { uint64_t address; uint32_t size; int32_t prot; } SegmentInfo;
typedef struct {
    uint64_t st_size;
    char name[256];
    int32_t id;
    uint32_t tls_index;
    uint64_t tls_init_addr;
    uint32_t tls_init_size, tls_size, tls_offset, tls_align;
    uint64_t init_proc_addr, fini_proc_addr, reserved1, reserved2;
    uint64_t eh_frame_hdr_addr, eh_frame_addr;
    uint32_t eh_frame_hdr_size, eh_frame_size;
    SegmentInfo segments[4];
    uint32_t segment_count;
} ModuleInfoEx;   /* OrbisKernelModuleInfoEx */

#define MAX_REGIONS 17
typedef struct {
    char name[32];
    uintptr_t base, end;           /* the module's part of the image */
    uintptr_t code, code_size;     /* its executable segment */
    int scanned;
    uintptr_t hdr, eh_frame;
    uint32_t hdr_size, eh_frame_size;
} Region;
static Region regions[MAX_REGIONS];
extern uintptr_t kr_unwind_test_ret;
static int unwind_trace;
static int region_count;
static pthread_mutex_t scan_lock = PTHREAD_MUTEX_INITIALIZER;

void runtime_unwind_add_region(const char *name, uintptr_t base, uintptr_t end, uintptr_t code,
                               uintptr_t code_size) {
    if (region_count >= MAX_REGIONS || end <= base) return;
    Region *r = &regions[region_count++];
    snprintf(r->name, sizeof(r->name), "%s", name);
    if (region_count == 1 && kr_unwind_test_ret && kr_unwind_test_ret < base) kr_unwind_test_ret += base;
    r->base = base; r->end = end; r->code = code; r->code_size = code_size;
}

static int32_t s32_at(uintptr_t p) { int32_t v; memcpy(&v, (const void *)p, 4); return v; }
static uint32_t u32_at(uintptr_t p) { uint32_t v; memcpy(&v, (const void *)p, 4); return v; }

/* The .eh_frame_hdr of a region: the header pattern whose table holds code addresses of the region
 * in ascending order and whose .eh_frame pointer stays inside it (the largest table wins). */
static void scan(Region *r) {
    r->scanned = 1;
    const uintptr_t code_end = r->code + r->code_size;
    uint32_t best = 0;
    for (uintptr_t p = r->code; p + 12 <= code_end; p += 4) {
        const unsigned char *b = (const unsigned char *)p;
        if (b[0] != 1 || b[1] != 0x1b || b[2] != 0x03 || b[3] != 0x3b) continue;
        const uintptr_t eh_frame = p + 4 + (intptr_t)s32_at(p + 4);
        const uint32_t count = u32_at(p + 8);
        if (eh_frame < r->base || eh_frame >= r->end || !count || count > 4000000) continue;
        if (p + 12 + (uint64_t)count * 8 > code_end) continue;
        const uintptr_t first = p + (intptr_t)s32_at(p + 12);
        const uintptr_t last = p + (intptr_t)s32_at(p + 12 + (uint64_t)(count - 1) * 8);
        if (first < r->code || last >= code_end || last < first) continue;
        if (count > best) {
            best = count;
            r->hdr = p;
            r->hdr_size = 12 + count * 8;
            r->eh_frame = eh_frame;
            /* .eh_frame ends where the header starts when it comes first (the usual layout). */
            r->eh_frame_size = (uint32_t)(eh_frame < p ? p - eh_frame : code_end - eh_frame);
        }
    }
    printf("Runtime: unwind tables of %s: %s", r->name, best ? "" : "none found\n");
    if (best) printf(".eh_frame_hdr at image+%#lx (%u functions), .eh_frame %u bytes\n",
                     (unsigned long)(r->hdr - regions[0].base), best, r->eh_frame_size);
}

static Region *find(uintptr_t address) {
    for (int i = 0; i < region_count; ++i) {
        Region *r = &regions[i];
        if (address >= r->base && address < r->end) {
            pthread_mutex_lock(&scan_lock);
            if (!r->scanned) scan(r);
            pthread_mutex_unlock(&scan_lock);
            return r;
        }
    }
    return NULL;
}

static ABI int32_t module_info_for_unwind(uintptr_t address, int32_t flags, UnwindInfo *info) {
    if (unwind_trace) {
        static int traced;
        const Region *q = find(address);
        if (traced++ < 32) printf("Runtime: unwind query %s+%#lx\n", q ? q->name : "(no module)",
                                  (unsigned long)(q ? address - q->base : address));
    }
    if (flags >= 3) { if (info) memset(info, 0, sizeof(*info)); return ERR(E_INVAL); }
    if (!info) return ERR(E_FAULT);
    if (info->st_size < sizeof(*info)) return ERR(E_INVAL);
    const Region *r = find(address);
    if (!r) return ERR(E_SRCH);
    memset(info, 0, sizeof(*info));
    info->st_size = sizeof(*info);
    snprintf(info->name, sizeof(info->name), "%s", r->name);
    info->eh_frame_hdr_addr = r->hdr;
    info->eh_frame_addr = r->eh_frame;
    info->eh_frame_size = r->eh_frame_size;
    info->seg0_addr = r->code;
    info->seg0_size = r->code_size;
    return 0;
}

static ABI int32_t module_info_from_addr(uintptr_t address, int32_t flags, ModuleInfoEx *info) {
    if (flags >= 3) { if (info) memset(info, 0, sizeof(*info)); return ERR(E_INVAL); }
    if (!info) return ERR(E_FAULT);
    const Region *r = find(address);
    if (!r) return ERR(E_SRCH);
    memset(info, 0, sizeof(*info));
    info->st_size = sizeof(*info);
    snprintf(info->name, sizeof(info->name), "%s", r->name);
    info->id = (int32_t)(r - regions);
    info->eh_frame_hdr_addr = r->hdr;
    info->eh_frame_addr = r->eh_frame;
    info->eh_frame_hdr_size = r->hdr_size;
    info->eh_frame_size = r->eh_frame_size;
    info->segments[0].address = r->code;
    info->segments[0].size = (uint32_t)r->code_size;
    info->segments[0].prot = 5;   /* read, execute */
    info->segment_count = 1;
    return 0;
}

typedef struct { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; } Phdr;
typedef struct { uint64_t addr; const char *name; const Phdr *phdr; uint16_t phnum; } PhdrInfo;
/* FreeBSD libc: 1 when addr is inside one of the module's executable PT_LOAD segments. */
static ABI int32_t elf_phdr_match_addr(const PhdrInfo *info, uintptr_t address) {
    if (!info || !info->phdr) return 0;
    for (uint16_t i = 0; i < info->phnum; ++i) {
        const Phdr *ph = &info->phdr[i];
        if (ph->type != 1 || !(ph->flags & 1)) continue;   /* PT_LOAD, PF_X */
        const uintptr_t lo = (uintptr_t)(info->addr + ph->vaddr);
        if (address >= lo && address < lo + ph->memsz) return 1;
    }
    return 0;
}

static ABI int32_t is_signal_return(uintptr_t pc) { (void)pc; return 0; }

static ABI int32_t sig_return(const void *context) {
    static int told;
    if (!__atomic_exchange_n(&told, 1, __ATOMIC_RELAXED))
        printf("Runtime: sigreturn(%p) called: not supported, refused\n", context);
    return -1;
}


/* KR_UNWIND_TEST=<eboot return offset> (a check of the above against the game's own unwinder):
 * the first scePthreadSetaffinity call returning to that offset throws std::bad_alloc instead,
 * exactly as the game's operator new does (eboot 0x255650: __cxa_allocate_exception(8), the
 * bad_alloc vtable + 0x10, __cxa_throw(exception, &typeid(bad_alloc), destructor)). The stub
 * jumps to __cxa_throw with the guest call site as its return address, so the unwinder walks
 * only game frames: it must find the call site's try range and land in its catch. Offsets of
 * this dump (v01.02): libc.prx __cxa_allocate_exception image+0xcbca30, __cxa_throw
 * image+0xcbd180; the eboot's GOT slots of the bad_alloc vtable, typeinfo and destructor at
 * image+0xac4178/0xac4180/0xac4188. */
uintptr_t kr_unwind_test_ret, kr_unwind_test_orig, kr_unwind_test_throw, kr_unwind_test_ti,
          kr_unwind_test_dtor;
unsigned char kr_unwind_test_done;
ABI uintptr_t kr_unwind_test_prepare(void) {
    const uintptr_t image = regions[0].base;
    typedef ABI void *(*Allocate)(uint64_t);
    void *exception = ((Allocate)(image + 0xcbca30))(8);
    uintptr_t vtable, typeinfo, destructor;
    memcpy(&vtable, (const void *)(image + 0xac4178), 8);
    memcpy(&typeinfo, (const void *)(image + 0xac4180), 8);
    memcpy(&destructor, (const void *)(image + 0xac4188), 8);
    vtable += 0x10;
    memcpy(exception, &vtable, 8);
    kr_unwind_test_ti = typeinfo;
    kr_unwind_test_dtor = destructor;
    kr_unwind_test_throw = image + 0xcbd180;
    unwind_trace = 1;
    printf("Runtime: KR_UNWIND_TEST: throwing std::bad_alloc at the call returning to eboot+%#lx\n",
           (unsigned long)(kr_unwind_test_ret - image));
    fflush(stdout);
    return (uintptr_t)exception;
}
__asm__(
    ".text\n"
    ".globl kr_unwind_test_stub\n"
    "kr_unwind_test_stub:\n"
    "  mov (%rsp), %rax\n"
    "  cmp kr_unwind_test_ret(%rip), %rax\n"
    "  jne 1f\n"
    "  mov $1, %al\n"
    "  xchg %al, kr_unwind_test_done(%rip)\n"
    "  test %al, %al\n"
    "  jne 1f\n"
    "  sub $8, %rsp\n"
    "  call kr_unwind_test_prepare\n"
    "  add $8, %rsp\n"
    "  mov %rax, %rdi\n"
    "  mov kr_unwind_test_ti(%rip), %rsi\n"
    "  mov kr_unwind_test_dtor(%rip), %rdx\n"
    "  jmp *kr_unwind_test_throw(%rip)\n"
    "1:\n"
    "  jmp *kr_unwind_test_orig(%rip)\n");
void kr_unwind_test_stub(void);
uintptr_t runtime_unwind_test_hook(const char *name, uintptr_t original) {
    static long ret = -1;
    if (ret < 0) { const char *e = getenv("KR_UNWIND_TEST"); ret = e && *e ? strtol(e, NULL, 0) : 0; }
    if (!ret || !original || !runtime_nid_is(name, "bt3CTBKmGyI")) return original;
    kr_unwind_test_orig = original;
    kr_unwind_test_ret = (uintptr_t)ret + (region_count ? regions[0].base : 0);
    return (uintptr_t)kr_unwind_test_stub;
}

uintptr_t runtime_krunwind_resolve(const char *name) {
    if (runtime_nid_is(name, "RpQJJVKTiFM")) return (uintptr_t)module_info_for_unwind;
    if (runtime_nid_is(name, "f7KBOafysXo")) return (uintptr_t)module_info_from_addr;
    if (runtime_nid_is(name, "Fjc4-n1+y2g")) return (uintptr_t)elf_phdr_match_addr;
    if (runtime_nid_is(name, "crb5j7mkk1c")) return (uintptr_t)is_signal_return;
    if (runtime_nid_is(name, "mo0bFmWppIw")) return (uintptr_t)sig_return;
    return 0;
}
