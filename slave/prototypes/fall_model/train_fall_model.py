# Trains the fall-confirmation CNN on SisFall and writes fall_model_weights.b64 +
# model_meta.json next to this file. Run in Colab (GPU) or locally:
#   pip install numpy torch kagglehub && python3 train_fall_model.py
# Then generate the C headers: python3 export_model.py <path>/SisFall_dataset
#
# The model only ever sees the windows the firmware would hand it, so the data
# comes from a simulation of slave/prototypes/bmi270_fall_detect.cpp on SisFall:
# any-motion wake -> 1 s wait -> every 2 s window in the new frames -> heuristic
# -> per wake, the passing window with the strongest impact.
import base64, json, os, re, time, zlib
import numpy as np
import kagglehub
import torch, torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- 1. Raw SisFall -> 50 Hz, accel in g (ADXL345) and gyro in dps (ITG3200) ----
root = os.path.join(kagglehub.dataset_download("kushajm/sisfall-dataset-fall-detection"), "SisFall_dataset")
ACC = 2 * 16 / 2**13      # ADXL345 +-16 g, 13 bits
GYR = 2 * 2000 / 2**16    # ITG3200 +-2000 dps, 16 bits
recs = []  # (name, activity, subject, array[n, 6])
for subj in sorted(os.listdir(root)):
    d = os.path.join(root, subj)
    if not os.path.isdir(d): continue
    for f in sorted(os.listdir(d)):
        m = re.match(r'([DF]\d\d)_(S[AE]\d\d)_R(\d\d)\.txt$', f)
        if not m: continue
        txt = open(os.path.join(d, f)).read().replace(';', ',').replace('\n', '')
        v = np.array([int(x) for x in txt.split(',') if x.strip()], dtype=np.float32)
        v = v[: v.size // 9 * 9].reshape(-1, 9)
        x = np.hstack([v[:, 0:3] * ACC, v[:, 3:6] * GYR])
        n = x.shape[0] // 4 * 4
        recs.append((f[:-4], m.group(1), m.group(2), x[:n].reshape(-1, 4, 6).mean(1).astype(np.float32)))
print(len(recs), "recordings;", sum(r[1][0] == 'F' for r in recs), "falls")

# ---- 2. Firmware flow simulation (mirrors bmi270_fall_detect.cpp / fall_pipeline.h) ----
HZ, WIN, STRIDE, POST, FIFO_FULL = 50, 100, 5, 50, 168
ORIENT = HZ // 2
IMPACT_G, TILT_DEG = 1.6, 40.0

def anymotion(a, thr=0.25):
    """BMI270 any-motion (datasheet 4.8.2): |a - reference| > thr, reference updated on detection."""
    ref = a[0].copy(); out = np.zeros(len(a), bool)
    for i in range(1, len(a)):
        if (np.abs(a[i] - ref) > thr).any():
            out[i] = True; ref = a[i].copy()
    return out

def heuristic(w):
    amag = np.linalg.norm(w[:, :3], axis=1)
    peak = float(amag.max())
    b = w[:ORIENT, :3].mean(0); e = w[-ORIENT:, :3].mean(0)
    c = np.clip(b @ e / (np.linalg.norm(b) * np.linalg.norm(e) + 1e-12), -1, 1)
    tilt = float(np.degrees(np.arccos(c)))
    return peak, tilt, peak >= IMPACT_G and tilt >= TILT_DEG

def simulate(x):
    """Returns (wakes, passing): wakes = best candidate window end per wake
    (or None), passing = all window ends that passed the heuristic."""
    det = anymotion(x[:, :3]); i = 1; prev_end = 0; wakes, passing = [], []
    while True:
        hits = np.nonzero(det[i:])[0]
        if hits.size == 0: break
        t = i + hits[0]; total = t + POST + 1
        if total > len(x): break
        first_new = max(prev_end, total - FIFO_FULL)
        last_end = max(WIN, first_new + 1); best, best_peak = None, -1
        end = total
        while end >= last_end:
            peak, tilt, ok = heuristic(x[end - WIN:end])
            if ok:
                passing.append(end)
                if peak > best_peak: best, best_peak = end, peak
            end -= STRIDE
        wakes.append(best)
        prev_end = total; i = total + 1
    return wakes, passing

t0 = time.time()
sim = [simulate(r[3]) for r in recs]
print(f"simulated in {time.time()-t0:.0f}s")
fall = [k for k, r in enumerate(recs) if r[1][0] == 'F']; adl = [k for k, r in enumerate(recs) if r[1][0] == 'D']
hpass = lambda k: any(e is not None for e in sim[k][0])
print(f"heuristic: falls {np.mean([hpass(k) for k in fall]):.1%}, ADL recordings {np.mean([hpass(k) for k in adl]):.1%}")

# ---- 3. Dataset of heuristic-passing windows, split by subject ----
TEST = {"SA03", "SA08", "SA13", "SA18", "SA23", "SE03", "SE08", "SE13"}
VAL = {"SA05", "SA10", "SA15", "SA20", "SE05", "SE10", "SE15"}
split_of = lambda s: "test" if s in TEST else "val" if s in VAL else "train"

ACC_SCALE, GYR_SCALE = 0.5, 1 / 200.0   # input scaling, mirrored in C

def model_input(w, gyro=True):
    """Orientation-invariant channels: |a|, a along initial gravity, a across it, |w|."""
    a = w[:, :3].astype(np.float64)
    g0 = a[:ORIENT].mean(0); u = g0 / np.linalg.norm(g0)
    amag = np.linalg.norm(a, axis=1); av = a @ u
    ah = np.sqrt(np.maximum(amag**2 - av**2, 0))
    ch = [amag * ACC_SCALE, av * ACC_SCALE, ah * ACC_SCALE]
    if gyro: ch.append(np.linalg.norm(w[:, 3:], axis=1) * GYR_SCALE)
    return np.stack(ch).astype(np.float32)

def impact_idx(x):
    return int(np.linalg.norm(x[:, :3], axis=1).argmax())

samples = {"train": [], "val": [], "test": []}  # (window end, rec idx, label)
for k, (name, act, subj, x) in enumerate(recs):
    sp = split_of(subj); isf = act[0] == 'F'; imp = impact_idx(x)
    for end in sim[k][1]:
        if isf and not (end - WIN <= imp < end):
            continue  # fall recording but window misses the fall: ambiguous, skip
        samples[sp].append((end, k, int(isf)))
for sp, s in samples.items():
    y = np.array([t[2] for t in s]); print(sp, len(s), "windows,", y.sum(), "fall")

# ---- 4. Train a small 1D CNN on the candidate windows ----
dev = "cuda" if torch.cuda.is_available() else "cpu"

def hard_negatives(sp):
    """ADL windows passing a looser gate (1.3 g, 25 deg): more negatives near the boundary."""
    out = []
    for k, (name, act, subj, x) in enumerate(recs):
        if act[0] != 'D' or split_of(subj) != sp: continue
        amag = np.linalg.norm(x[:, :3], axis=1)
        for end in range(WIN, len(x) + 1, STRIDE):
            w = x[end - WIN:end]
            if amag[end - WIN:end].max() >= 1.3:
                b = w[:ORIENT, :3].mean(0); e = w[-ORIENT:, :3].mean(0)
                c = np.clip(b @ e / (np.linalg.norm(b) * np.linalg.norm(e)), -1, 1)
                if np.degrees(np.arccos(c)) >= 25: out.append((end, k, 0))
    return out
hn = hard_negatives("train"); print("hard negatives (train):", len(hn))

def tensors(s, gyro):
    X = np.stack([model_input(recs[k][3][e - WIN:e], gyro) for e, k, _ in s])
    return torch.tensor(X), torch.tensor([t[2] for t in s], dtype=torch.float32)

class FallNet(nn.Module):
    def __init__(self, cin):
        super().__init__()
        self.f = nn.Sequential(nn.Conv1d(cin, 8, 5), nn.ReLU(), nn.MaxPool1d(2),
                               nn.Conv1d(8, 16, 5), nn.ReLU(), nn.MaxPool1d(2),
                               nn.Conv1d(16, 16, 5), nn.ReLU())
        self.out = nn.Linear(16, 1)
    def forward(self, x):
        return self.out(self.f(x).mean(2)).squeeze(1)

def train(gyro, epochs=60, seed=0):
    torch.manual_seed(seed)
    Xtr, ytr = tensors(samples["train"] + hn, gyro)
    Xtr, ytr = Xtr.to(dev), ytr.to(dev)
    net = FallNet(Xtr.shape[1]).to(dev)
    pos_w = (ytr == 0).sum() / (ytr == 1).sum()
    lossf = nn.BCEWithLogitsLoss(pos_weight=pos_w)
    opt = torch.optim.Adam(net.parameters(), 3e-3, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, epochs)
    for ep in range(epochs):
        net.train(); perm = torch.randperm(len(Xtr), device=dev)
        for i in range(0, len(perm), 256):
            idx = perm[i:i + 256]; xb = Xtr[idx].clone()
            xb[:, :3] *= torch.empty(len(idx), 1, 1, device=dev).uniform_(0.85, 1.15)  # sensor gain
            if gyro: xb[:, 3] *= torch.empty(len(idx), 1, device=dev).uniform_(0.8, 1.2)
            xb += 0.005 * torch.randn_like(xb)
            loss = lossf(net(xb), ytr[idx]); opt.zero_grad(); loss.backward(); opt.step()
        sched.step()
    return net.eval().cpu()

def wake_probs(net, sp, gyro):
    """Per recording: model probability of each wake's candidate window (what the ESP runs)."""
    out = {}
    for k, (name, act, subj, x) in enumerate(recs):
        if split_of(subj) != sp: continue
        ends = [e for e in sim[k][0] if e is not None]
        if ends:
            X = torch.tensor(np.stack([model_input(x[e - WIN:e], gyro) for e in ends]))
            with torch.no_grad(): out[k] = torch.sigmoid(net(X)).numpy()
        else:
            out[k] = np.zeros(0)
    return out

def event_metrics(probs, thr):
    """Share of fall recordings alerted, ADL recordings alerted, elderly ADL recordings alerted."""
    f = [p.size > 0 and p.max() >= thr for k, p in probs.items() if recs[k][1][0] == 'F']
    d = [p.size > 0 and p.max() >= thr for k, p in probs.items() if recs[k][1][0] == 'D']
    de = [p.size > 0 and p.max() >= thr for k, p in probs.items() if recs[k][1][0] == 'D' and recs[k][2][1] == 'E']
    return np.mean(f), np.mean(d), np.mean(de)

results = {}
for gyro in (True, False):
    results[gyro] = train(gyro)

# ---- 5. Threshold curve (val and test) for each variant ----
for gyro, net in results.items():
    pv, pt = wake_probs(net, "val", gyro), wake_probs(net, "test", gyro)
    print(f"gyro={gyro}  thr: VAL falls/ADL | TEST falls/ADL")
    for thr in (0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95):
        v, t = event_metrics(pv, thr), event_metrics(pt, thr)
        print(f"  {thr:.2f}: {v[0]:.1%}/{v[1]:.1%} | {t[0]:.1%}/{t[1]:.1%}")
print("params:", sum(p.numel() for p in results[True].parameters()))

# ---- 6. Export: C-style reference forward pass, weights and golden windows ----
# Chosen on VAL: recall is flat at 99.7% for thresholds 0.2-0.9; 0.5 sits in the middle.
GYRO, THRESHOLD = True, 0.5
net = results[GYRO]
P = {n: p.detach().numpy().astype(np.float32) for n, p in net.state_dict().items()}

def conv_relu(x, w, b):            # x [cin][t], w [cout][cin][k]: valid conv + ReLU, float32 loops
    cout, cin, k = w.shape; t = x.shape[1] - k + 1
    y = np.zeros((cout, t), np.float32)
    for o in range(cout):
        for i in range(t):
            acc = np.float32(b[o])
            for c in range(cin):
                for j in range(k):
                    acc = np.float32(acc + w[o, c, j] * x[c, i + j])
            y[o, i] = max(acc, np.float32(0))
    return y
pool = lambda x: np.maximum(x[:, 0:x.shape[1] // 2 * 2:2], x[:, 1:x.shape[1] // 2 * 2:2])
def c_forward(x):
    h = pool(conv_relu(x, P["f.0.weight"], P["f.0.bias"]))
    h = pool(conv_relu(h, P["f.3.weight"], P["f.3.bias"]))
    h = conv_relu(h, P["f.6.weight"], P["f.6.bias"])
    z = np.float32(P["out.bias"][0] + (P["out.weight"][0] * h.mean(1)).sum())
    return float(1 / (1 + np.exp(-z)))

# golden windows: candidates from test subjects (2 falls, 2 ADL incl. the ADL with highest prob)
pt = wake_probs(net, "test", GYRO)
gold = []
fk = [k for k in pt if recs[k][1][0] == 'F' and pt[k].size][:40:20]
dk = sorted([k for k in pt if recs[k][1][0] == 'D' and pt[k].size], key=lambda k: -pt[k].max())
for k in fk + [dk[0], dk[len(dk) // 2]]:
    ends = [e for e in sim[k][0] if e is not None]; e = ends[int(np.argmax(pt[k]))]
    w = recs[k][3][e - WIN:e]
    peak, tilt, ok = heuristic(w)
    xin = model_input(w, GYRO)
    with torch.no_grad(): pt_prob = float(torch.sigmoid(net(torch.tensor(xin[None])))[0])
    cp = c_forward(xin)
    gold.append(dict(name=recs[k][0], end=int(e), peak=peak, tilt=tilt, passes=ok, prob=cp))
    print(f"{recs[k][0]}: peak {peak:.2f} tilt {tilt:.1f} pass {ok} prob torch {pt_prob:.6f} c-style {cp:.6f}")

order = ["f.0.weight", "f.0.bias", "f.3.weight", "f.3.bias", "f.6.weight", "f.6.bias", "out.weight", "out.bias"]
blob = base64.b64encode(zlib.compress(np.concatenate([P[n].ravel() for n in order]).astype(np.float32).tobytes(), 9))
open(os.path.join(HERE, "fall_model_weights.b64"), "w").write(blob.decode())
meta = dict(threshold=THRESHOLD, acc_scale=ACC_SCALE, gyr_scale=GYR_SCALE,
            shapes={n: list(P[n].shape) for n in order}, gold=gold)
json.dump(meta, open(os.path.join(HERE, "model_meta.json"), "w"), indent=2)
print("wrote fall_model_weights.b64 and model_meta.json")
