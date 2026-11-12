#!/usr/bin/env bash
# Runs the game with a route script and frame captures.
# Usage: run_route.sh <name> <route-script> <seconds> [ENV=VALUE ...]
# The route script is sourced with: press <btn> | hold <tokens> <s> | shot | w <s>; output in out/val/<name>/
set -u
name=$1; route=$2; secs=$3; shift 3
d=$HOME/kr-port/out/val/$name; rm -rf "$d"; mkdir -p "$d"
P=$d/pad; T=$d/trig; : > "$P"
(
  press() { echo "$1" > "$P"; sleep 0.25; : > "$P"; sleep 0.35; }
  hold() { echo "$1" > "$P"; sleep "$2"; : > "$P"; sleep 0.2; }
  shot() { echo "SHOT $(date +%s.%N) ${1:-}" >> "$d/shots.txt"; touch "$T"; sleep 1.0; }
  w() { sleep "$1"; }
  source "$route"
) &
cd "$HOME/kr-port/port" && env "$@" BB_PAD_FILE="$P" BB_PRESENT_DUMP_TRIGGER="$T" BB_DUMP_DIR="$d" \
  timeout $((secs+15)) ./out/bb-probe "$HOME/kr-port/out/boot-linked.bin" --app0 "$HOME/kr-port/game" \
  --user "$HOME/kr-port/user" --timeout "$secs" > "$d/log.txt" 2>&1
wait
cd "$d" && for f in f*_present_*.raw; do [ -e "$f" ] || continue; sz=$(echo "$f" | grep -oE '[0-9]+x[0-9]+' | head -1); magick -size "${sz:-1920x1080}" -depth 8 bgra:"$f" -alpha off -resize 640x360 "${f%%_*}.png" && rm "$f"; done
ls "$d"/*.png 2>/dev/null | wc -l
