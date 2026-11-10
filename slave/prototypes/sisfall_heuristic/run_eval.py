# Usage: run_eval.py <dir with simulate.py> <sisfall50.npz> <any-motion thr g> <post-trigger s> <out.pkl> [dense]
import sys, numpy as np
sys.path.insert(0, sys.argv[1]); from simulate import load, wakes, wakes_dense
recs = load(sys.argv[2])
thr, post_s = float(sys.argv[3]), float(sys.argv[4])
rows = []
for name, x in recs:
    w = (wakes_dense if len(sys.argv) > 6 else wakes)(x, thr, post_s)
    rows.append((name, w, len(x) / 50.0))
import pickle; pickle.dump(rows, open(sys.argv[5], 'wb'))  # our own file, not dataset data
fall = [r for r in rows if r[0][0] == 'F']; adl = [r for r in rows if r[0][0] == 'D']
mins = sum(r[2] for r in adl) / 60
print(f"thr={thr} post={post_s}s  falls with >=1 wake: {sum(1 for r in fall if r[1])}/{len(fall)}  "
      f"ADL wakes/min: {sum(len(r[1]) for r in adl)/mins:.1f}")
def rule(f, P=2.5, F=0.6, W=150, T=45):
    peak, mb, dps, tilt, post, p = f
    return peak >= P and (mb <= F or dps >= W or tilt >= T)
det = sum(1 for r in fall if any(rule(f) for f in r[1])); fa = sum(1 for r in adl if any(rule(f) for f in r[1]))
print(f"current rule: falls passed {det}/{len(fall)} ({det/len(fall):.1%}), ADL recordings passed {fa}/{len(adl)} ({fa/len(adl):.1%})")
