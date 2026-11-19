#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Collects the corresponding source code of every third-party binary in the release AppImage.

Adapted from battride-war-recomp's scripts/release/collect_sources.py. Run as root inside the
Ubuntu 22.04 build image (tools/appimage/Dockerfile) right after build_appimage.py, with network
access (tools/appimage/build_portable.sh --sources does both). It:
- maps each file the build copied from the image (app/system-files.txt) to its Ubuntu package and
  downloads the source packages of exactly the installed versions;
- adds the upstream sources the image built into /usr/local (FFmpeg, SDL3) and the libraries
  compiled into bb-probe and libbbgpu.so (fmt, Zydis, ...), at the versions the Dockerfile and
  port/gpu/CMakeLists.txt pin;
- adds the AppImage runtime and the libraries compiled into it;
and writes one tar for the release page, with an index in SOURCES.md.

The port's own source is this repository at the release tag.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request

# Keep in sync with tools/appimage/Dockerfile, the README's xbyak/VMA step and port/gpu/CMakeLists.txt.
GH = 'https://github.com/%s/archive/refs/tags/%s.tar.gz'
UPSTREAM = (
    # (file, url, sha256 or None, what it is)
    ('FFmpeg-n8.1.3.tar.gz', GH % ('FFmpeg/FFmpeg', 'n8.1.3'), None,
     'FFmpeg (LGPL-2.1-or-later as configured: no --enable-gpl): libavcodec, libavformat, libavutil, '
     'libswscale and libswresample in usr/lib; built by the Dockerfile'),
    ('SDL-release-3.4.20.tar.gz', GH % ('libsdl-org/SDL', 'release-3.4.20'), None,
     'SDL 3 (zlib license): libSDL3 in usr/lib; built by the Dockerfile'),
    ('fmt-12.2.1.tar.gz', GH % ('fmtlib/fmt', '12.2.1'), None, 'fmt (MIT): compiled into libbbgpu.so'),
    ('zydis-v4.1.1.tar.gz', GH % ('zyantific/zydis', 'v4.1.1'), None, 'Zydis (MIT): compiled into libbbgpu.so'),
    ('zycore-c-0b2432ced0884fd152b471d97ecf0258ff4d859f.tar.gz',
     'https://github.com/zyantific/zycore-c/archive/0b2432ced0884fd152b471d97ecf0258ff4d859f.tar.gz', None,
     'Zycore (MIT), the commit Zydis v4.1.1 pins: compiled into libbbgpu.so'),
    ('robin-map-v1.4.1.tar.gz', GH % ('Tessil/robin-map', 'v1.4.1'), None,
     'tsl::robin_map (MIT, header-only): compiled into libbbgpu.so'),
    ('magic_enum-v0.9.7.tar.gz', GH % ('Neargye/magic_enum', 'v0.9.7'), None,
     'magic_enum (MIT, header-only): compiled into libbbgpu.so'),
    ('miniz-3.1.2.tar.gz', GH % ('richgel999/miniz', '3.1.2'), None, 'miniz (MIT): compiled into libbbgpu.so'),
    ('xbyak-v7.24.tar.gz', GH % ('herumi/xbyak', 'v7.24'), None,
     'xbyak (BSD-3-Clause, header-only): compiled into libbbgpu.so'),
    ('VulkanMemoryAllocator-v3.3.0.tar.gz', GH % ('GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator', 'v3.3.0'), None,
     'Vulkan Memory Allocator (MIT, header-only): compiled into libbbgpu.so'),
    ('Vulkan-Headers-vulkan-sdk-1.4.363.0.tar.gz', GH % ('KhronosGroup/Vulkan-Headers', 'vulkan-sdk-1.4.363.0'),
     None, 'Vulkan headers (Apache-2.0 / MIT): compiled into libbbgpu.so'),
    ('boost_1_86_0.tar.gz', 'https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz',
     '2575e74ffc3ef1cd0babac2c1ee8bdb5782a0ee672b1912da40e5b4b591ca01f',
     'Boost 1.86.0 (BSL-1.0, headers only): compiled into libbbgpu.so'),
    # The AppImage runtime (tools: runtime-x86_64 = type2-runtime "continuous" at this commit) and
    # the libraries its build compiles in (scripts/common/install-dependencies.sh there).
    ('type2-runtime-fedfad51acc2860b2916a1eef32ba4a500ff31ef.tar.gz',
     'https://github.com/AppImage/type2-runtime/archive/fedfad51acc2860b2916a1eef32ba4a500ff31ef.tar.gz', None,
     'the AppImage runtime (MIT); see scripts/common/install-dependencies.sh and patches/ in it'),
    ('fuse-3.15.0.tar.xz', 'https://github.com/libfuse/libfuse/releases/download/fuse-3.15.0/fuse-3.15.0.tar.xz',
     '70589cfd5e1cff7ccd6ac91c86c01be340b227285c5e200baa284e401eea2ca0',
     'libfuse (LGPL-2.1 for the library): linked statically into the runtime'),
    ('squashfuse-0.5.2.tar.gz', 'https://github.com/vasi/squashfuse/archive/0.5.2.tar.gz',
     'db0238c5981dabbd80ee09ae15387f390091668ca060a7bc38047912491443d3',
     'squashfuse (BSD-2-Clause): linked statically into the runtime'),
)
# Files under /usr/local come from the Dockerfile's source builds above.
LOCAL = {'libav': 'FFmpeg-n8.1.3.tar.gz', 'libsw': 'FFmpeg-n8.1.3.tar.gz', 'libSDL3': 'SDL-release-3.4.20.tar.gz'}


def fetch(url, dest, sha256=None):
    with urllib.request.urlopen(url, timeout=300) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if sha256 and digest != sha256:
        raise SystemExit('Checksum mismatch for ' + url)
    dest.write_bytes(data)
    return digest


def owners():
    """Every packaged path, also under its merged-/usr real location, to its binary package."""
    table = {}
    for listing in Path('/var/lib/dpkg/info').glob('*.list'):
        package = listing.stem.split(':')[0]
        for line in listing.read_text(errors='replace').splitlines():
            if line:
                table.setdefault(line, package)
                real = os.path.join(os.path.realpath(os.path.dirname(line)), os.path.basename(line))
                table.setdefault(real, package)
    return table


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--stage', type=Path, required=True, help='the AppDir build_appimage.py made')
    ap.add_argument('--out', type=Path, required=True, help='output .tar (must not exist)')
    args = ap.parse_args()
    if args.out.exists():
        raise SystemExit('Output exists: ' + str(args.out))
    work = args.out.with_suffix('.d')
    if work.exists():
        raise SystemExit('Work directory exists: ' + str(work))
    ubuntu, upstream = work / 'ubuntu', work / 'upstream'
    ubuntu.mkdir(parents=True)
    upstream.mkdir()

    files = [l for l in (args.stage / 'app' / 'system-files.txt').read_text().splitlines() if l]
    table = owners()
    by_package, local, unowned = collections.defaultdict(list), collections.defaultdict(list), []
    for path in files:
        package = table.get(path) or table.get(os.path.realpath(path))
        if package:
            by_package[package].append(path)
        elif path.startswith('/usr/local/'):
            name = os.path.basename(path)
            tarball = next((t for prefix, t in LOCAL.items() if name.startswith(prefix)), None)
            if not tarball:
                raise SystemExit('No upstream source known for ' + path)
            local[tarball].append(path)
        else:
            unowned.append(path)
    fields = subprocess.check_output(
        ['dpkg-query', '-W', '-f=${Package}\t${source:Package}\t${source:Version}\t${Version}\n'], text=True)
    source_of = {}
    for line in fields.splitlines():
        package, src, src_version, version = line.split('\t')
        source_of[package] = (src or package, src_version or version)
    sources = collections.defaultdict(set)
    for package in by_package:
        sources[source_of[package]].add(package)

    # deb-src entries mirroring the image's binary ones.
    lines = [l.replace('deb ', 'deb-src ', 1) for l in Path('/etc/apt/sources.list').read_text().splitlines()
             if l.startswith('deb ')]
    Path('/etc/apt/sources.list.d/battride-sources.list').write_text('\n'.join(lines) + '\n')
    subprocess.run(['apt-get', 'update', '-qq'], check=True)
    failed = []
    for src, version in sorted(sources):
        if subprocess.run(['apt-get', 'source', '--download-only', '-qq', '%s=%s' % (src, version)],
                          cwd=ubuntu).returncode:
            failed.append('%s=%s' % (src, version))
    if failed:
        raise SystemExit('Source packages not available: ' + ', '.join(failed))

    digests = {}
    for name, url, sha256, _ in UPSTREAM:
        print('Downloading', name, flush=True)
        digests[name] = fetch(url, upstream / name, sha256)

    package = json.loads((args.stage / 'app' / 'package.json').read_text())
    version = package['version']
    out = ['# Corresponding source for Battride War Genesis %s' % version, '',
           'Source code of every third-party binary in `BattrideWarGenesis-%s-x86_64.AppImage`. The port\'s own '
           'code (the launcher, `bb-probe`, `libbbgpu.so` and the preparation scripts) is the project repository '
           'at tag `v%s`: https://github.com/robertoqimin/battride-war-sousei-port/tree/v%s' % (version, version,
                                                                                               version),
           '', '## Ubuntu 22.04 source packages (`ubuntu/`)', '',
           'Each entry is a Debian source package (`.dsc` with its tarballs) of exactly the version installed in '
           'the build image (`tools/appimage/Dockerfile`). Unpack with `dpkg-source -x <file>.dsc`.', '',
           '| Source package | Version | Binary packages used |', '|---|---|---|']
    for src, ver in sorted(sources):
        out.append('| %s | %s | %s |' % (src, ver, ', '.join(sorted(sources[(src, ver)]))))
    out += ['', '## Upstream sources (`upstream/`)', '']
    for name, url, _, what in UPSTREAM:
        out.append('- `%s`: %s. From %s (SHA-256 `%s`).' % (name, what, url, digests[name]))
    out += ['', '## Files not owned by a package', '',
            'Generated in the build image, not shipped by a package (for example the compiled GSettings schema):',
            '']
    out += ['- `%s`' % p for p in unowned] or ['- none']
    (work / 'SOURCES.md').write_text('\n'.join(out) + '\n')

    with tarfile.open(args.out, 'x') as tar:
        tar.add(work, arcname=args.out.stem)
    print('Sources: %s (%d source packages, %d upstream archives)' % (args.out, len(sources), len(UPSTREAM)))


if __name__ == '__main__':
    main()
