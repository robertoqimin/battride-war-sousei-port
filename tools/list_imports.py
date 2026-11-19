#!/usr/bin/env python3
"""List PS4 imports (library, NID) of a plain ELF rebuilt from an eboot SELF."""
import struct, sys, json
d = open(sys.argv[1], 'rb').read()
phoff, = struct.unpack_from('<Q', d, 0x20); phnum, = struct.unpack_from('<H', d, 0x38)
ph = [struct.unpack_from('<IIQQQQQQ', d, phoff + i*56) for i in range(phnum)]
dynlib = next(p for p in ph if p[0] == 0x61000000)[2]
dyn = next(p for p in ph if p[0] == 2)
tags = {}
for i in range(dyn[5] // 16):
    t, v = struct.unpack_from('<qQ', d, dyn[2] + i*16)
    tags.setdefault(t, []).append(v)
strtab = dynlib + tags[0x61000035][0]; symtab = dynlib + tags[0x61000039][0]; symsz = tags[0x6100003f][0]
s = lambda o: d[strtab+o:d.index(b'\0', strtab+o)].decode()
B = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-'
enc = lambda n: (B[n] if n < 64 else enc(n // 64) + B[n % 64])
libs = {enc(v >> 48): s(v & 0xffffffff) for v in tags.get(0x61000015, []) + tags.get(0x61000049, [])}
mods = {enc(v >> 48): s(v & 0xffffffff) for v in tags.get(0x6100000f, [])}
out = []
for i in range(symsz // 24):
    name, info, other, shndx, val, size = struct.unpack_from('<IBBHQQ', d, symtab + i*24)
    n = s(name)
    if shndx == 0 and '#' in n:
        nid, l, m = n.split('#')
        out.append({'nid': nid, 'lib': libs.get(l, l), 'module': mods.get(m, m), 'kind': 'data' if info & 0xf == 1 else 'func'})
json.dump(out, sys.stdout, indent=0)
