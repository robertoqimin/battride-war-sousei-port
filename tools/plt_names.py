#!/usr/bin/env python3
"""Map PS4 import PLT stubs/GOT slots to names. Usage: plt_names.py ELF COVERAGE_JSON BASE"""
import struct, sys, json, re
d = open(sys.argv[1], 'rb').read(); cov = json.load(open(sys.argv[2])); base = int(sys.argv[3], 0)
names = {x['nid']: x['name'] for x in cov}
phoff, = struct.unpack_from('<Q', d, 0x20); phnum, = struct.unpack_from('<H', d, 0x38)
ph = [struct.unpack_from('<IIQQQQQQ', d, phoff + i*56) for i in range(phnum)]
dynlib = next(p for p in ph if p[0] == 0x61000000)[2]; dyn = next(p for p in ph if p[0] == 2)
tags = {}
for i in range(dyn[5] // 16):
    t, v = struct.unpack_from('<qQ', d, dyn[2] + i*16); tags.setdefault(t, []).append(v)
strtab = dynlib + tags[0x61000035][0]; symtab = dynlib + tags[0x61000039][0]
s = lambda o: d[strtab+o:d.index(b'\0', strtab+o)].decode()
def sym(i): return s(struct.unpack_from('<I', d, symtab + i*24)[0])
got = {}
for tag_off, tag_sz in ((0x61000029, 0x6100002d), (0x6100002f, 0x61000031)):  # JMPREL, RELA
    off, sz = dynlib + tags[tag_off][0], tags[tag_sz][0]
    for i in range(sz // 24):
        r_off, info, add = struct.unpack_from('<QQq', d, off + i*24)
        if info & 0xffffffff in (7, 1, 6):  # JUMP_SLOT, 64, GLOB_DAT
            n = sym(info >> 32)
            if '#' in n: got[r_off] = names.get(n.split('#')[0], n.split('#')[0])
text = next(p for p in ph if p[0] == 1 and p[1] & 1)
stubs = {}
for m in re.finditer(b'\xff\x25', d[text[2]:text[2]+text[5]]):
    va = text[3] + m.start(); tgt = va + 6 + struct.unpack_from('<i', d, text[2] + m.start() + 2)[0]
    if tgt in got: stubs[va] = got[tgt]
out = {'stubs': {hex(a+base): n for a, n in sorted(stubs.items())}, 'got': {hex(a+base): n for a, n in sorted(got.items())}}
json.dump(out, sys.stdout, indent=1)
print(f"\n# {len(stubs)} PLT stubs, {len(got)} GOT slots", file=sys.stderr)
