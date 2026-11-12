#!/usr/bin/env python3
"""List the game's imports that the kr-port runtime does not implement (by NID or name).
Usage: missing_imports.py  (run from ~/kr-port; writes out/missing_imports.json)"""
import json, glob, collections, re
names = json.load(open('out/link.json'))['unresolved_imports']
aero = dict(re.findall(r'STUB\(\s*"([^"]{11})"\s*,\s*([A-Za-z0-9_]+)\s*\)',
                       open('refs/shadPS4/src/core/aerolib/aerolib.inl').read()))
cov = {x['nid']: x for x in json.load(open('imports_v102_coverage.json'))}
src = ''
for pat in ('port/src/**/*.c', 'port/gpu/**/*.cpp', 'port/gpu/**/*.h', 'port/gpu/**/*.c'):
    for f in glob.glob(pat, recursive=True):
        if '/bbnet20/' in f:   # online module: only with BB_ONLINE=1, not the offline path
            continue
        src += open(f, errors='ignore').read()
libof = {}
for f in glob.glob('refs/shadPS4/src/core/libraries/**/*.cpp', recursive=True):
    for nid, lib in re.findall(r'LIB_FUNCTION\("([^"]{11})",\s*"([^"]+)"', open(f, errors='ignore').read()):
        libof.setdefault(nid, lib)
missing = collections.defaultdict(set)
for n in names:
    nid = n.split('#')[0]
    name = cov.get(nid, {}).get('name') or aero.get(nid, nid)
    if '"' + nid in src or f'"{name}"' in src:
        continue
    missing[cov.get(nid, {}).get('lib') or libof.get(nid, '?')].add(name)
print(f"{len(names)} imports left to runtime; {sum(map(len, missing.values()))} not implemented\n")
for lib, v in sorted(missing.items(), key=lambda kv: -len(kv[1])):
    v = sorted(v)
    print(f"{len(v):4} {lib}: {', '.join(v[:10])}{' ...' if len(v) > 10 else ''}")
json.dump({k: sorted(v) for k, v in missing.items()}, open('out/missing_imports.json', 'w'), indent=1)
