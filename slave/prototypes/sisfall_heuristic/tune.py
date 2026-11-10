# Usage: tune.py <features.pkl from run_eval.py>
import sys, pickle, itertools, numpy as np
rows = pickle.load(open(sys.argv[1], 'rb'))
names = [r[0] for r in rows]; isfall = np.array([n[0] == 'F' for n in names])
subj = [n.split('_')[1] for n in names]; act = [n.split('_')[0] for n in names]
dur = np.array([r[2] for r in rows])
test = np.array([int(s[2:]) % 2 == 0 for s in subj])  # even subjects held out
F, rec = [], []
for i, r in enumerate(rows):
    for f in r[1]: F.append(f[:5]); rec.append(i)
F = np.array(F); rec = np.array(rec); R = len(rows)
peak, mb, dps, tilt, post = F.T
def evaluate(mask, sel):
    hit = np.zeros(R, bool); np.logical_or.at(hit, rec[mask], True)
    fs, ad = isfall & sel, ~isfall & sel
    sens = hit[fs].mean(); fa = hit[ad].mean()
    cand_h = mask[np.isin(rec, np.nonzero(ad)[0])].sum() / (dur[ad].sum() / 3600)
    return sens, fa, cand_h, hit
inf = 1e9
grid = dict(P=np.arange(1.6, 4.01, 0.2), F=[0.3, 0.4, 0.5, 0.6, 0.7, 0.8, -1],
            W=[80, 120, 160, 200, 250, 300, inf], T=[20, 30, 40, 50, 60, inf], S=[0.1, 0.15, 0.2, 0.3, 0.5, inf])
train = ~test; best = []
for P, S_ in itertools.product(grid['P'], grid['S']):
    base = (peak >= P) & (post <= S_)
    for Fv, W, T in itertools.product(grid['F'], grid['W'], grid['T']):
        m = base & ((mb <= Fv) | (dps >= W) | (tilt >= T))
        s, fa, ch, _ = evaluate(m, train)
        best.append((s, fa, ch, P, S_, Fv, W, T))
best = np.array(best)
for target in (0.99, 0.98, 0.97):
    ok = best[best[:, 0] >= target]
    b = ok[np.lexsort((ok[:, 2], ok[:, 1]))][0]
    P, S_, Fv, W, T = b[3:]
    m = (peak >= P) & (post <= S_) & ((mb <= Fv) | (dps >= W) | (tilt >= T))
    st, fat, cht, hit = evaluate(m, test)
    print(f"target>={target:.0%}: P={P:.1f} postStd<={S_:g} | minBefore<={Fv:g} or gyro>={W:g} or tilt>={T:g}")
    print(f"    train sens {b[0]:.1%} ADL-rec {b[1]:.1%} cand/h {b[2]:.0f}   TEST sens {st:.1%} ADL-rec {fat:.1%} cand/h {cht:.0f}")
cur = (peak >= 2.5) & ((mb <= 0.6) | (dps >= 150) | (tilt >= 45))
st, fat, cht, hit = evaluate(cur, test); print(f"current rule TEST sens {st:.1%} ADL-rec {fat:.1%} cand/h {cht:.0f}")
