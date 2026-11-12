#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# kr-port: reads a KR_GAME_PROFILE sample file and reports, per thread, where the time goes:
# the game's own code (by function), the port's runtime (by the import the game called), or blocked
# in the kernel. Busy windows (1 s) of the main thread are reported on their own.
#
# usage: game_profile.py <samples.txt> <text.asm> <plt.json> [--top N] [--busy PCT]
#   text.asm: objdump -d of the eboot's code with eboot offsets as addresses (as in this project)
#   plt.json: {"stubs": {addr: name}} with Ghidra addresses (offset + 0x100000)
import bisect
import collections
import json
import re
import sys


def load_functions(asm_path):
    """Function starts: `push rbp` directly followed by `mov rbp,rsp` (the game keeps frame pointers)."""
    starts = []
    prev = None
    calls = {}
    line_re = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)$")
    with open(asm_path, errors="replace") as f:
        for line in f:
            m = line_re.match(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            text = m.group(2).strip()
            if text.startswith("mov") and "rbp,rsp" in text.replace(" ", "") and prev and prev[1] == "push   rbp":
                starts.append(prev[0])
            if text.startswith("call") and "0x" in text:
                target = text.split("0x")[-1].split()[0]
                try:
                    calls[addr] = int(target, 16)
                except ValueError:
                    pass
            prev = (addr, text)
    starts.sort()
    return starts, calls


def main():
    args = sys.argv[1:]
    top = 15
    busy_pct = 80.0
    if "--top" in args:
        i = args.index("--top"); top = int(args[i + 1]); del args[i:i + 2]
    if "--busy" in args:
        i = args.index("--busy"); busy_pct = float(args[i + 1]); del args[i:i + 2]
    samples_path, asm_path, plt_path = args[:3]
    starts, calls = load_functions(asm_path)
    stubs = json.load(open(plt_path))["stubs"]
    plt = {}
    for k, v in (stubs.items() if isinstance(stubs, dict) else []):
        plt[(int(k, 16) if isinstance(k, str) else k) - 0x100000] = v
    # call instructions end where the return address points: index them by their end
    call_end = {}
    for addr, target in calls.items():
        call_end[addr + 5] = target

    def function_of(off):
        i = bisect.bisect_right(starts, off) - 1
        return starts[i] if i >= 0 else 0

    def import_of(ret):
        target = call_end.get(ret)
        if target is None:
            return "runtime (call site %#x)" % ret
        name = plt.get(target)
        if name:
            return name
        return "runtime via %#x" % target  # a small wrapper (e.g. jmp to the import)

    names = {}
    per_thread = collections.defaultdict(lambda: collections.Counter())
    detail = collections.defaultdict(lambda: collections.Counter())
    windows = collections.defaultdict(lambda: collections.Counter())  # (tid, second) -> kinds
    window_detail = collections.defaultdict(lambda: collections.Counter())
    rows = []
    with open(samples_path, errors="replace") as f:
        for line in f:
            if line.startswith("#"):
                continue
            p = line.split()
            if p and p[0] == "T":
                names[int(p[1])] = " ".join(p[2:])
                continue
            if len(p) < 4:
                continue
            ms, tid, rip, sc = float(p[0]), int(p[1]), p[2], p[3]
            rets = [int(x, 16) for x in p[4:] if not x.startswith("@")]
            port_frames = [x[1:] for x in p[4:] if x.startswith("@")]
            host = rip[2:].split("+")[0] if rip.startswith("h:") else None
            first = next((r for r in rets if r), 0)
            if sc == "s":
                kind = "blocked"
                what = "blocked in " + (import_of(first) if first else (host or "?"))
            elif rip.startswith("h"):
                kind = "runtime"
                what = import_of(first) if first else "port: %s" % (host or "?")
                if first and host:
                    what += "  [%s]" % host
            else:
                kind = "game"
                what = "fn %#x" % function_of(int(rip, 16))
            per_thread[tid][kind] += 1
            detail[tid][(kind, what)] += 1
            sec = int(ms // 1000)
            windows[(tid, sec)][kind] += 1
            window_detail[(tid, sec)][(kind, what)] += 1

    # The game's thread names; several threads may share one (TaskJobThread_1...).
    def label(tid):
        return "%s:%d" % (names.get(tid, "?"), tid)

    print("== Time per thread (samples ~1 per ms)")
    for tid, c in sorted(per_thread.items(), key=lambda kv: -sum(kv[1].values())):
        total = sum(c.values())
        busy = c["game"] + c["runtime"]
        print("%-28s %7d samples  game %5.1f%%  runtime %5.1f%%  blocked %5.1f%%" % (
            label(tid), total, 100 * c["game"] / total, 100 * c["runtime"] / total, 100 * c["blocked"] / total))
    for tid, c in sorted(per_thread.items(), key=lambda kv: -(kv[1]["game"] + kv[1]["runtime"]))[:4]:
        total = sum(c.values())
        print("\n== %s: top entries (%% of its samples)" % label(tid))
        for (kind, what), n in detail[tid].most_common(top):
            print("  %5.1f%%  %-8s %s" % (100 * n / total, kind, what))

    # Busy seconds of the main thread
    main_tids = [t for t, n in names.items() if n == "main"]
    for tid in main_tids:
        secs = sorted(s for (t, s) in windows if t == tid)
        busy = []
        for s in secs:
            c = windows[(tid, s)]
            total = sum(c.values())
            if total and 100 * (c["game"] + c["runtime"]) / total >= busy_pct:
                busy.append(s)
        print("\n== main: %d of %d seconds at least %.0f%% busy: %s" % (len(busy), len(secs), busy_pct,
              " ".join(str(s) for s in busy[:60])))
        agg = collections.Counter()
        for s in busy:
            agg.update(window_detail[(tid, s)])
        total = sum(agg.values())
        if total:
            print("   where those seconds go:")
            kinds = collections.Counter()
            for (kind, _), n in agg.items():
                kinds[kind] += n
            print("   game %.1f%%  runtime %.1f%%  blocked %.1f%%" % tuple(100 * kinds[k] / total for k in ("game", "runtime", "blocked")))
            for (kind, what), n in agg.most_common(top):
                print("  %5.1f%%  %-8s %s" % (100 * n / total, kind, what))


if __name__ == "__main__":
    main()
