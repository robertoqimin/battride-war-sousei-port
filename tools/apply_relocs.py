#!/usr/bin/env python3
"""Bake PS4 DT_SCE_RELA relocations into an ELF for static analysis at a fixed base.
R_X86_64_RELATIVE -> base+addend; R_X86_64_64/GLOB_DAT to imports -> PLT stub (if known).
Usage: apply_relocs.py IN.elf OUT.elf BASE PLT_JSON"""
import struct, sys, json
src, dst, base, pltj = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), sys.argv[4]
d = bytearray(open(src, 'rb').read())
phoff, = struct.unpack_from('<Q', d, 0x20); phnum, = struct.unpack_from('<H', d, 0x38)
ph = [struct.unpack_from('<IIQQQQQQ', d, phoff + i*56) for i in range(phnum)]
dynlib = next(p for p in ph if p[0] == 0x61000000)[2]; dyn = next(p for p in ph if p[0] == 2)
tags = {}
for i in range(dyn[5] // 16):
    t, v = struct.unpack_from('<qQ', d, dyn[2] + i*16); tags.setdefault(t, []).append(v)
strtab = dynlib + tags[0x61000035][0]; symtab = dynlib + tags[0x61000039][0]
s = lambda o: bytes(d[strtab+o:d.index(b'\0', strtab+o)]).decode()
stubs = json.load(open(pltj))['stubs']
name2stub = {}
for a, n in stubs.items(): name2stub.setdefault(n, int(a, 0) - base)
cov = {x['nid']: x['name'] for x in json.load(open(pltj.replace('plt_v102.json', 'imports_v102_coverage.json')))}
def v2o(va):
    for p in ph:
        if p[0] in (1, 0x61000010) and p[3] <= va < p[3] + p[5]: return p[2] + va - p[3]
    return None
applied = skipped = bss = 0
for tag_off, tag_sz in ((0x6100002f, 0x61000031), (0x61000029, 0x6100002d)):
    off, sz = dynlib + tags[tag_off][0], tags[tag_sz][0]
    for i in range(sz // 24):
        r, info, add = struct.unpack_from('<QQq', d, off + i*24); t = info & 0xffffffff
        o = v2o(r)
        if o is None: bss += 1; continue
        if t == 8: struct.pack_into('<Q', d, o, base + add); applied += 1
        elif t in (1, 6, 7):
            nid = sym = s(struct.unpack_from('<I', d, symtab + (info >> 32)*24)[0]).split('#')[0]
            tgt = name2stub.get(cov.get(nid, nid))
            if tgt is not None: struct.pack_into('<Q', d, o, base + tgt + (add if t == 1 else 0)); applied += 1
            else: skipped += 1
open(dst, 'wb').write(d)
print(f'applied {applied}, unresolved import refs {skipped}, targets outside file data (bss) {bss}')
