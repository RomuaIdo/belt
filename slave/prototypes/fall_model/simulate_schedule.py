# Compares wake schedules on the SisFall test subjects with the trained model:
# old (any-motion -> stay awake 1 s -> read) vs new (idle: any-motion wake;
# active: timer read every 1 s; idle again after N s without any-motion).
# Usage: python3 simulate_schedule.py <path>/SisFall_dataset .
import sys, os, json, zlib, base64, re, numpy as np
from numpy.lib.stride_tricks import sliding_window_view as swv
root, mdir = sys.argv[1], sys.argv[2]
meta = json.load(open(os.path.join(mdir, "model_meta.json")))
flat = np.frombuffer(zlib.decompress(base64.b64decode(open(os.path.join(mdir, "fall_model_weights.b64")).read())), np.float32)
P, off = {}, 0
for k, sh in meta["shapes"].items():
    n = int(np.prod(sh)); P[k] = flat[off:off + n].reshape(sh); off += n
ACC, GYR = 2 * 16 / 2**13, 2 * 2000 / 2**16
HZ, WIN, STRIDE, FIFO_FULL, ORIENT = 50, 100, 5, 168, 25
TEST = {"SA03", "SA08", "SA13", "SA18", "SA23", "SE03", "SE08", "SE13"}

def model_input(w):
    a = w[:, :3].astype(np.float64); g0 = a[:ORIENT].mean(0); u = g0 / np.linalg.norm(g0)
    amag = np.linalg.norm(a, axis=1); av = a @ u; ah = np.sqrt(np.maximum(amag**2 - av**2, 0))
    return np.stack([amag * .5, av * .5, ah * .5, np.linalg.norm(w[:, 3:], axis=1) * .005])
def conv(x, w, b):
    return np.maximum(np.einsum('ctk,ock->ot', swv(x, w.shape[2], axis=1), w) + b[:, None], 0)
pool = lambda x: np.maximum(x[:, 0:x.shape[1] // 2 * 2:2], x[:, 1:x.shape[1] // 2 * 2:2])
def prob(w):
    h = pool(conv(model_input(w), P["f.0.weight"], P["f.0.bias"]))
    h = pool(conv(h, P["f.3.weight"], P["f.3.bias"])); h = conv(h, P["f.6.weight"], P["f.6.bias"])
    return 1 / (1 + np.exp(-(P["out.bias"][0] + P["out.weight"][0] @ h.mean(1))))
def anymotion(a, thr=0.25):
    ref = a[0].copy(); out = np.zeros(len(a), bool)
    for i in range(1, len(a)):
        if (np.abs(a[i] - ref) > thr).any(): out[i] = True; ref = a[i].copy()
    return out
def heuristic(w):
    amag = np.linalg.norm(w[:, :3], axis=1); b = w[:ORIENT, :3].mean(0); e = w[-ORIENT:, :3].mean(0)
    c = np.clip(b @ e / (np.linalg.norm(b) * np.linalg.norm(e) + 1e-12), -1, 1)
    return amag.max(), amag.max() >= 1.6 and np.degrees(np.arccos(c)) >= 40

def process(x, total, prev_end):
    """Checks windows ending in the frames read at `total`; returns model prob of the best candidate or None."""
    first_new = max(prev_end, total - FIFO_FULL); last_end = max(WIN, first_new + 1)
    best, bp = None, -1
    for end in range(total, last_end - 1, -STRIDE):
        pk, ok = heuristic(x[end - WIN:end])
        if ok and pk > bp: best, bp = end, pk
    return None if best is None else prob(x[best - WIN:best])

def old_scheme(x, det):
    i, prev_end, probs, awake = 1, 0, [], 0
    while True:
        h = np.nonzero(det[i:])[0]
        if not h.size: break
        total = i + h[0] + 51
        if total > len(x): break
        p = process(x, total, prev_end); probs.append(p); awake += 50
        prev_end = total; i = total + 1
    return probs, awake, len(probs)

def new_scheme(x, det, idle_after):
    i, prev_end, probs, wakes = 1, 0, [], 0
    while True:
        h = np.nonzero(det[i:])[0]
        if not h.size: break
        total = i + h[0] + 1; last_motion = total - 1          # any-motion wake: read at once
        while total <= len(x):
            probs.append(process(x, total, prev_end)); wakes += 1; prev_end = total
            if total - last_motion >= idle_after: break        # back to idle
            nxt = total + HZ                                   # timer: 1 s later
            m = np.nonzero(det[total:min(nxt, len(x))])[0]
            if m.size: last_motion = total + m[-1]
            total = nxt
        i = prev_end + 1
    return probs, 1, wakes

res = {}
for subj in sorted(os.listdir(root)):
    if subj not in TEST: continue
    for f in sorted(os.listdir(os.path.join(root, subj))):
        if not re.match(r'[DF]\d\d_S[AE]\d\d_R\d\d\.txt$', f): continue
        v = np.array([int(t) for t in open(os.path.join(root, subj, f)).read().replace(';', ',').replace('\n', '').split(',') if t.strip()], np.float32)
        v = v[: v.size // 9 * 9].reshape(-1, 9); xx = np.hstack([v[:, :3] * ACC, v[:, 3:6] * GYR]); n = len(xx) // 4 * 4
        x = xx[:n].reshape(-1, 4, 6).mean(1).astype(np.float32); det = anymotion(x[:, :3])
        res[f[:3]] = res.get(f[:3], []) + [(old_scheme(x, det), {k: new_scheme(x, det, k * HZ) for k in (3, 5, 10)}, len(x) / HZ)]
def report(label, get):
    fa = [any(p is not None and p >= .5 for p in get(r)[0]) for a, rs in res.items() if a[0] == 'F' for r in rs]
    da = [any(p is not None and p >= .5 for p in get(r)[0]) for a, rs in res.items() if a[0] == 'D' for r in rs]
    mins = sum(r[2] for a, rs in res.items() if a[0] == 'D' for r in rs) / 60
    wakes = sum(get(r)[2] for a, rs in res.items() if a[0] == 'D' for r in rs)
    print(f"{label}: falls {np.mean(fa):.1%} | ADL recordings alerted {np.mean(da):.1%} | ESP wakes/min during ADL {wakes / mins:.1f}")
report("old (any-motion + 1 s awake)", lambda r: r[0])
for k in (3, 5, 10): report(f"new, idle after {k:2d} s still", lambda r, k=k: r[1][k])
