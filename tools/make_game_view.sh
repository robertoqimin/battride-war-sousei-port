#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Builds the merged game view the port runs from: a tree of symbolic links to YOUR dump of the
# base game (CUSA02716 v01.00) with the update (v01.02) on top. Nothing is copied or modified.
# Usage: tools/make_game_view.sh <base-dump-dir> <patch-dump-dir> [<out-dir>=./game]
set -euo pipefail
base=$(realpath "$1"); patch=$(realpath "$2"); out=${3:-game}
[[ -f $base/eboot.bin && -f $patch/eboot.bin ]] || { echo "expected eboot.bin in both dumps" >&2; exit 1; }
[[ -e $out ]] && { echo "$out already exists" >&2; exit 1; }
mkdir -p "$out"
cp -rs "$base/." "$out/"
cp -rsf "$patch/." "$out/"
echo "game view ready: $out (eboot from the update)"
