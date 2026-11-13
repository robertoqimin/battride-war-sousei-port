#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Builds the launcher AppImage: the launcher, the built port (bb-probe, libbbgpu.so), the game
preparation scripts, and the Python + GTK 4 + libadwaita they run on, with every library they
need except the ones that must be the host's (libc, Vulkan and GL drivers, X11/Wayland).

Adapted from battride-war-recomp's scripts/build_appimage.py. No game code or data is packed:
the player's own dump is prepared on their computer by the launcher.

The AppImage runs on systems whose glibc is at least the build system's (it uses the build
system's libraries). Built inside an older distribution (Ubuntu 22.04, as battride-war-recomp
does) it runs on more systems; this script does not require it and says which floor it built for.

usage: tools/appimage/build_appimage.py --tools ~/tools/appimage [--output out/appimage/...]
  --tools: a folder with appimagetool-x86_64.AppImage, runtime-x86_64 and patchelf/bin/patchelf
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--tools', type=Path, required=True)
ap.add_argument('--output', type=Path, default=REPO / 'out' / 'appimage' / 'BattrideWarGenesis-x86_64.AppImage')
ap.add_argument('--stage', type=Path, default=REPO / 'out' / 'appimage' / 'AppDir')
args = ap.parse_args()
S = args.stage.resolve()
PATCHELF = str(args.tools / 'patchelf' / 'bin' / 'patchelf')
APPIMAGETOOL = str(args.tools / 'appimagetool-x86_64.AppImage')
RUNTIME = args.tools / 'runtime-x86_64'
for tool in (PATCHELF, APPIMAGETOOL, RUNTIME):
    if not Path(tool).is_file():
        raise SystemExit('Missing tool: %s' % tool)
PROBE = REPO / 'port' / 'out' / 'bb-probe'
GPU_LIB = REPO / 'port' / 'out' / 'gpu' / 'libbbgpu.so'
if not PROBE.is_file() or not GPU_LIB.is_file():
    raise SystemExit('Build the port first (port/build.sh).')
if S.exists():
    shutil.rmtree(S)   # the stage is ours (out/ is generated); the output is replaced at the end


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    dst.chmod(dst.stat().st_mode | 0o200)   # writable: patchelf and strip change the copies


def copytree(src, dst, **kw):
    shutil.copytree(src, dst, symlinks=False, **kw)


# Libraries that must match the host's GPU driver and system: never shipped (as in
# battride-war-recomp, plus the SDL3/Vulkan loaders' driver side).
def host_library(name):
    return name.startswith((
        'ld-linux', 'libc.so', 'libm.so', 'libmvec.so', 'libpthread.so', 'libdl.so', 'librt.so', 'libresolv.so',
        'libnss_', 'libutil.so', 'libanl.so', 'libstdc++', 'libgcc_s', 'libatomic.so',
        'libvulkan', 'libGL.so', 'libGLX', 'libGLdispatch', 'libglapi', 'libEGL', 'libOpenGL',
        'libdrm', 'libgbm', 'libLLVM', 'libSPIRV', 'libudev', 'libexpat', 'libelf', 'libz.so', 'libzstd',
        'libX11.so', 'libX11-xcb', 'libXau', 'libXdmcp', 'libXext', 'libXfixes', 'libXxf86vm', 'libxshmfence',
        'libxcb.so', 'libxcb-dri2', 'libxcb-dri3', 'libxcb-present', 'libxcb-sync', 'libxcb-xfixes',
        'libxcb-randr', 'libxcb-shm', 'libxcb-glx', 'libwayland-client', 'libwayland-server', 'libwayland-egl',
        'libsystemd', 'libva', 'libvdpau', 'libOpenCL'))


print('Staging', S, flush=True)
# --- The launcher, the scripts and the port ---------------------------------------------
for name in ('launcher.py', 'core.py'):
    copy(REPO / 'tools' / 'launcher' / name, S / 'app' / name)
for script in sorted((REPO / 'port' / 'scripts').glob('*.py')):
    copy(script, S / 'app' / 'scripts' / script.name)
copy(REPO / 'CREDITS.md', S / 'app' / 'CREDITS.md')
copy(PROBE, S / 'usr' / 'bin' / 'bb-probe')
copy(GPU_LIB, S / 'usr' / 'bin' / 'gpu' / 'libbbgpu.so')
# The development build keeps debug information (about 200 MiB in libbbgpu.so): not shipped.
subprocess.run(['strip', '--strip-debug', str(S / 'usr' / 'bin' / 'bb-probe'),
                str(S / 'usr' / 'bin' / 'gpu' / 'libbbgpu.so')], check=True)
for name, src in (('port/LICENSE', REPO / 'LICENSE'), ('port/port-LICENSE-GPL', REPO / 'port' / 'LICENSE'),
                  ('port/NOTICE', REPO / 'port' / 'NOTICE'), ('port/NOTICE.bbport', REPO / 'port' / 'NOTICE.bbport'),
                  ('port/CREDITS.md', REPO / 'CREDITS.md')):
    if src.is_file():
        copy(src, S / 'usr' / 'share' / 'licenses' / name)

# --- Python, PyGObject, GTK 4 / libadwaita data ------------------------------------------
version = '%d.%d' % sys.version_info[:2]
pylib = S / 'usr' / 'lib' / ('python' + version)
copy(Path(sys.executable).resolve(), S / 'usr' / 'bin' / 'python3')
copytree(sysconfig.get_path('stdlib'), pylib,
         ignore=shutil.ignore_patterns('site-packages', 'dist-packages', '__pycache__', 'test', 'tests', 'idlelib',
                                       'ensurepip', 'venv', 'tkinter', 'turtledemo', 'lib2to3', 'pydoc_data',
                                       '_tkinter*'))   # Tk is not used (and pulls Tcl/Tk in)
for module in ('gi', 'cairo'):
    found = next(Path(p) / module for p in sys.path if (Path(p) / module).is_dir())
    copytree(found, pylib / 'site-packages' / module, ignore=shutil.ignore_patterns('__pycache__'))
typelibs = Path('/usr/lib/girepository-1.0')
wanted = ('Adw-1', 'cairo-1.0', 'freetype2-2.0', 'Gdk-4.0', 'GdkPixbuf-2.0', 'Gio-2.0', 'GioUnix-2.0', 'GLib-2.0',
          'GLibUnix-2.0', 'GModule-2.0', 'GObject-2.0', 'Graphene-1.0', 'Gsk-4.0', 'Gtk-4.0', 'HarfBuzz-0.0',
          'Pango-1.0', 'PangoCairo-1.0', 'PangoFc-1.0', 'PangoFT2-1.0', 'PangoOT-1.0', 'fontconfig-2.0',
          'GdkPixdata-2.0', 'Xlib-2.0', 'xlib-2.0', 'GdkWayland-4.0', 'GdkX11-4.0')
for name in wanted:
    if (typelibs / (name + '.typelib')).is_file():
        copy(typelibs / (name + '.typelib'), S / 'usr' / 'lib' / 'girepository-1.0' / (name + '.typelib'))
copy('/usr/share/glib-2.0/schemas/gschemas.compiled', S / 'usr' / 'share' / 'glib-2.0' / 'schemas' / 'gschemas.compiled')

# --- Every library the ELF files need, but the host's ------------------------------------
libdir = S / 'usr' / 'lib'
seeds = []
for pattern in ('libgtk-4.so.1', 'libadwaita-1.so.0', 'libgraphene-1.0.so.0', 'libpangocairo-1.0.so.0',
                'libpangoft2-1.0.so.0'):
    seeds.extend(Path('/usr/lib').glob(pattern))
queue = seeds + [p for p in S.rglob('*') if p.is_file() and not p.is_symlink()]
seen, sources, missing = set(), {}, set()
while queue:
    path = queue.pop()
    real = path.resolve()
    if real in seen:
        continue
    seen.add(real)
    try:
        with path.open('rb') as f:
            if f.read(4) != b'\x7fELF':
                continue
    except OSError:
        continue
    if not str(path).startswith(str(S)):
        if host_library(path.name):
            continue
        dest = libdir / path.name
        if not dest.exists():
            copy(real, dest)
            sources[path.name] = str(real)
        path = dest
    out = subprocess.run(['ldd', str(path)], capture_output=True, text=True).stdout
    for line in out.splitlines():
        match = re.search(r'=> (/\S+)', line) or re.match(r'\s*(/\S+)', line)
        if match and Path(match[1]).exists():
            queue.append(Path(match[1]))
        elif 'not found' in line:
            missing.add(line.strip())

# DT_RPATH (inherited by the libraries they load), relative: no LD_LIBRARY_PATH, which would
# leak these libraries into host programs started from here.
def set_rpath(path, rpath):
    subprocess.run([PATCHELF, '--force-rpath', '--set-rpath', rpath, str(path)], check=True)


set_rpath(S / 'usr' / 'bin' / 'python3', '$ORIGIN/../lib')
set_rpath(S / 'usr' / 'bin' / 'bb-probe', '$ORIGIN/gpu:$ORIGIN/../lib')
set_rpath(S / 'usr' / 'bin' / 'gpu' / 'libbbgpu.so', '$ORIGIN:$ORIGIN/../../lib')
for so in pylib.rglob('*.so'):
    set_rpath(so, '$ORIGIN/' + os.path.relpath(libdir, so.parent))
for so in libdir.iterdir():
    if so.is_file() and '.so' in so.name:
        dyn = subprocess.run(['readelf', '-d', str(so)], capture_output=True, text=True).stdout
        if 'RUNPATH' in dyn or 'RPATH' in dyn:
            set_rpath(so, '$ORIGIN')

glibc = subprocess.check_output(['getconf', 'GNU_LIBC_VERSION'], text=True).strip()
(S / 'app' / 'package.json').write_text(json.dumps({
    'glibc_min': glibc.split()[-1], 'python': version, 'libraries': len(sources),
    'bb_probe_sha256': sha(PROBE), 'libbbgpu_sha256': sha(GPU_LIB), 'runtime_sha256': sha(RUNTIME),
    'unresolved': sorted(missing)}, indent=2))
(S / 'app' / 'library-sources.json').write_text(json.dumps(sources, indent=2, sort_keys=True))

# --- AppRun, desktop entry, icon ----------------------------------------------------------
(S / 'AppRun').write_text('''#!/bin/sh
APPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export PORT_APPDIR="$APPDIR" PYTHONHOME="$APPDIR/usr" PYTHONPLATLIBDIR=lib
export PYTHONPATH="$APPDIR/app:$APPDIR/usr/lib/python%s/site-packages" PYTHONDONTWRITEBYTECODE=1
export GI_TYPELIB_PATH="$APPDIR/usr/lib/girepository-1.0" GSETTINGS_SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas"
export GSETTINGS_BACKEND=memory GIO_USE_VFS=local GIO_MODULE_DIR="$APPDIR/usr/lib/gio/modules"
unset LD_LIBRARY_PATH LD_PRELOAD
exec "$APPDIR/usr/bin/python3" "$APPDIR/app/launcher.py" "$@"
''' % version)
(S / 'AppRun').chmod(0o755)
(S / 'usr' / 'lib' / 'gio' / 'modules').mkdir(parents=True, exist_ok=True)
(S / 'battride-war-genesis.desktop').write_text(
    '[Desktop Entry]\nType=Application\nName=Kamen Rider: Battride War Genesis\n'
    'Comment=Native PC port (your own PS4 dump)\nExec=AppRun\nIcon=battride-war-genesis\nCategories=Game;\n'
    'Terminal=false\n')
# Our own icon (the game's is Bandai Namco's and is not shipped).
(S / 'battride-war-genesis.svg').write_text(
    '<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256"><rect width="256" height="256" rx="48" '
    'fill="#1b1b22"/><path d="M40 70 L128 40 L216 70 L196 196 L128 220 L60 196 Z" fill="#c8102e"/><text x="128" '
    'y="160" font-size="88" text-anchor="middle" fill="#ffffff" font-family="sans-serif" font-weight="bold">BW'
    '</text></svg>')
(S / '.DirIcon').symlink_to('battride-war-genesis.svg')

# --- Pack ----------------------------------------------------------------------------------
args.output.parent.mkdir(parents=True, exist_ok=True)
if args.output.exists():
    args.output.unlink()
env = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN='1', ARCH='x86_64')
subprocess.run([APPIMAGETOOL, '--no-appstream', '--runtime-file', str(RUNTIME), str(S), str(args.output)],
               check=True, env=env)
args.output.with_suffix('.AppImage.sha256').write_text(sha(args.output) + '  ' + args.output.name + '\n')
print('AppImage: %s (%.0f MiB, glibc %s or newer, %d bundled libraries)' %
      (args.output, args.output.stat().st_size / 2**20, glibc.split()[-1], len(sources)))
if missing:
    print('Unresolved libraries (review):', ', '.join(sorted(missing)))
