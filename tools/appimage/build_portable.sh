#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Builds the port and the launcher AppImage inside Ubuntu 22.04 (tools/appimage/Dockerfile), so the
# AppImage needs glibc 2.35 or newer instead of the build machine's. Needs Docker.
#   tools/appimage/build_portable.sh --tools ~/tools/appimage [--sources]
# --tools: the same folder as build_appimage.py's. The checkout is copied into out/portable-build
# (kept for faster rebuilds; your port/out is left alone); the AppImage lands in out/appimage/.
# --sources: also the release's corresponding-source tar (tools/appimage/collect_sources.py; needs
# network, several hundred MB), next to the AppImage.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
tools=
sources=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --tools) tools=$(cd -- "$2" && pwd); shift 2 ;;
        --sources) sources=1; shift ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ -n $tools ]] || { echo 'Usage: build_portable.sh --tools DIR' >&2; exit 2; }
image=battride-war-sousei-port-build:ubuntu22.04
docker build -t "$image" "$repo/tools/appimage"
mkdir -p "$repo/out/appimage" "$repo/out/portable-build"
# xbyak and VMA are header-only and not packaged: the folder the README sets up is reused.
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$repo:/src:ro" -v "$tools:/tools:ro" -v "$repo/out/appimage:/output" -v "$repo/out/portable-build:/tmp/build" \
    "$image" bash -euc '
        # A fresh copy of the sources (files deleted from the checkout must not linger); the
        # build folder (port/out) stays for faster rebuilds.
        find /tmp/build -mindepth 1 -maxdepth 1 ! -name port -exec rm -rf {} +
        if [[ -d /tmp/build/port ]]; then
            find /tmp/build/port -mindepth 1 -maxdepth 1 ! -name out -exec rm -rf {} +
        fi
        cd /src
        tar --exclude=./out --exclude=./port/out --exclude=./port/pgo --exclude=./game --exclude=./user \
            -cf - . | tar -xf - -C /tmp/build
        rm -rf /tmp/build/port/out/deps && mkdir -p /tmp/build/port/out && cp -a /src/port/out/deps /tmp/build/port/out/
        cd /tmp/build
        export CMAKE_INCLUDE_PATH=$PWD/port/out/deps CPLUS_INCLUDE_PATH=$PWD/port/out/deps BB_PGO=off
        # GCC 14 needs a newer libstdc++ than Ubuntu 22.04 has: linked in, its symbols kept private.
        export LDFLAGS="-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL"
        port/build.sh
        python3 tools/appimage/build_appimage.py --tools /tools --stage /tmp/build/out/appimage/AppDir
        cp out/appimage/*.AppImage out/appimage/*.AppImage.sha256 /output/
    '
version=$(sed -n "s/^VERSION = '\([^']*\)'.*/\1/p" "$repo/tools/launcher/core.py")
if [[ $sources == 1 ]]; then
    # As root (apt-get source); the tar is handed back to the user who ran this.
    name=BattrideWarGenesis-$version-sources.tar
    rm -rf "$repo/out/portable-build/out/appimage/${name%.tar}.d" "$repo/out/portable-build/out/appimage/$name"
    docker run --rm -v "$repo/out/appimage:/output" -v "$repo/out/portable-build:/tmp/build" "$image" bash -euc "
        cd /tmp/build/out/appimage
        python3 /tmp/build/tools/appimage/collect_sources.py --stage AppDir --out $name
        sha256sum $name > $name.sha256
        cp $name $name.sha256 /output/
        chown $(id -u):$(id -g) /output/$name /output/$name.sha256
        rm -rf ${name%.tar}.d $name $name.sha256
    "
fi
