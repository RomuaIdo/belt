# Usage: inspect_rule.py <features.pkl> <min peak g> <min tilt deg>
import sys, pickle, collections, numpy as np
rows = pickle.load(open(sys.argv[1], 'rb'))
P, T = float(sys.argv[2]), float(sys.argv[3])
def ok(f): return f[0] >= P and f[3] >= T
fall = [r for r in rows if r[0][0] == 'F']; adl = [r for r in rows if r[0][0] == 'D']
miss = collections.Counter(r[0][:3] for r in fall if not any(ok(f) for f in r[1]))
fa = collections.Counter(r[0][:3] for r in adl if any(ok(f) for f in r[1]))
tot = collections.Counter(r[0][:3] for r in adl)
hours = sum(r[2] for r in adl) / 3600
# model runs: one per wake that has >=1 passing window (approximated by stride groups of consecutive passing windows)
runs = sum(sum(1 for k, f in enumerate(r[1]) if ok(f) and (k == 0 or not ok(r[1][k - 1]))) for r in adl)
sens = 1 - sum(miss.values()) / len(fall)
print(f"P>={P} tilt>={T}: falls {sens:.1%} (missed {sum(miss.values())}) | ADL recordings {sum(fa.values())/len(adl):.1%} | ~model runs/h of ADL {runs/hours:.0f}")
print("  missed falls:", dict(sorted(miss.items())))
print("  ADL passing (of total):", {k: f"{fa[k]}/{tot[k]}" for k in sorted(fa)})
