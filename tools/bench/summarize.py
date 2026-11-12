"""kr-port: summary of a bench run. Frame stats windows (5 s each) are split into menus (before the
combat mark) and combat; reports FPS, worst frame, windows with a spike (worst > 1.5x median), GPU
idle, stalls, and the time from start to the combat mark."""
import json, re, sys
log = open(sys.argv[1], errors='replace').read().split('\n')
marks = {}
try:
    for l in open(sys.argv[2]):
        k, v = l.split(); marks[k] = float(v)
except OSError:
    pass
start = float(sys.argv[3])
wins = []
for l in log:
    m = re.search(r'Frame stats: ([0-9.]+) FPS, worst frame ([0-9.]+) ms.*?idle ([0-9.]+)%', l)
    if m: wins.append(tuple(float(x) for x in m.groups()))
# windows are 5 s apart; the combat mark tells which ones are combat
c0 = marks.get('BENCH_COMBAT_START'); c1 = marks.get('BENCH_COMBAT_END')
first = int((c0 - start) / 5) + 1 if c0 else len(wins)
last = int((c1 - start) / 5) if c1 else len(wins)
combat = wins[first:last] or wins[-6:]
def summary(ws):
    if not ws: return {}
    fps = [w[0] for w in ws]; worst = [w[1] for w in ws]
    med = 1000 / (sum(fps) / len(fps))
    return {'windows': len(ws), 'fps_mean': round(sum(fps)/len(fps), 2), 'fps_min': min(fps),
            'worst_frame_ms_max': max(worst), 'worst_frame_ms_median': sorted(worst)[len(worst)//2],
            'windows_with_spike': sum(1 for w in worst if w > 1.5 * med),
            'gpu_thread_idle_pct': round(sum(w[2] for w in ws)/len(ws), 1)}
stalls = sum(1 for l in log if l.startswith('Stall:'))
faults = [l for l in log if 'Host fault' in l or ('STOP:' in l and 'watchdog' not in l)]
print(json.dumps({'combat': summary(combat), 'all': summary(wins), 'stall_reports': stalls,
                  'seconds_to_combat': round(c0 - start, 1) if c0 else None,
                  'errors': faults[:3]}, indent=1))
