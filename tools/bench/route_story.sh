#!/usr/bin/env bash
# kr-port benchmark route: boot -> menus (circle) -> Story Mode -> tutorial pages (OPTIONS) -> combat.
# Usage: route_story.sh <pad-file>   (writes button tokens into BB_PAD_FILE's file)
P=$1; : > "$P"
press() { echo "$1" > "$P"; sleep 0.25; : > "$P"; }
sleep 6
for i in $(seq 1 20); do sleep 1; press circle; sleep 2.5; done
for i in 1 2 3; do sleep 1; press options; sleep 2; done
echo "BENCH_COMBAT_START $(date +%s.%N)" >> "$P.marks"
for i in $(seq 1 12); do
  echo "ly=0" > "$P"; sleep 1.5
  press square; sleep 0.15; press square; sleep 0.3; press triangle; sleep 0.3
  echo "lx=255" > "$P"; sleep 1.0; echo "lx=0 ly=0" > "$P"; sleep 1.0; : > "$P"; sleep 0.3
done
echo "BENCH_COMBAT_END $(date +%s.%N)" >> "$P.marks"
