#!/usr/bin/env bash
# kr-port benchmark: runs the fixed route and summarizes the log (tools/bench/summarize.py).
# Usage: bench.sh <name> [ENV=VALUE ...]   -> ~/kr-port/out/bench/<name>.{log,json}
set -u
name=$1; shift
out=$HOME/kr-port/out/bench; mkdir -p "$out"
pad=$out/$name.pad; rm -f "$pad.marks"
here=$(cd "$(dirname "$0")" && pwd)
"$here/route_story.sh" "$pad" &
route=$!
start=$(date +%s.%N)
( cd "$HOME/kr-port/port" && env "$@" BB_FRAME_STATS=1 BB_FPS_PRINT=1 BB_PAD_FILE="$pad" timeout 185 \
    ./out/bb-probe "$HOME/kr-port/out/boot-linked.bin" --app0 "$HOME/kr-port/game" --user "$HOME/kr-port/user" \
    --timeout 170 > "$out/$name.log" 2>&1 )
wait $route 2>/dev/null
python3 -I "$here/summarize.py" "$out/$name.log" "$pad.marks" "$start" > "$out/$name.json"
cat "$out/$name.json"
