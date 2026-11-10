# Emulates the belt firmware on SisFall at 50 Hz: any-motion wake -> wait post
# -> newest 2 s window -> features (same as bmi270_fall_detect.cpp).
import numpy as np
HZ, WIN = 50, 100

def load(path):
    z = np.load(path)
    names, lens, data = z['names'], z['lens'], z['data']
    offs = np.concatenate([[0], np.cumsum(lens)])
    return [(str(n), data[offs[i]:offs[i + 1]]) for i, n in enumerate(names)]

def features(w):
    a = np.linalg.norm(w[:, :3], axis=1); g = np.linalg.norm(w[:, 3:], axis=1)
    p = int(a.argmax()); peak = a[p]
    lb = HZ; mb = a[max(0, p - lb):p].min() if p > 0 else a[0]
    ar = HZ // 2; dps = g[max(0, p - ar):min(len(a), p + ar + 1)].max()
    k = HZ // 2; b = w[:k, :3].mean(0); e = w[-k:, :3].mean(0)
    c = np.clip(b @ e / (np.linalg.norm(b) * np.linalg.norm(e) + 1e-9), -1, 1)
    tilt = np.degrees(np.arccos(c))
    post = a[-k:].std()
    return peak, mb, dps, tilt, post, p

def wakes(x, thr=0.25, post_s=1.0):
    """Yields feature rows for each simulated wake in one recording."""
    slope = np.abs(np.diff(x[:, :3], axis=0)).max(1) > thr  # slope[i] = between i and i+1
    post = int(post_s * HZ); i = 1; out = []
    while True:
        hits = np.nonzero(slope[i - 1:])[0]
        if hits.size == 0: break
        t = i + hits[0]                 # trigger sample
        end = t + post + 1
        if end > len(x): break
        if end - WIN >= 0: out.append(features(x[end - WIN:end]))
        i = end + 1                     # latch cleared after handling
    return out

def anymotion(x, thr):
    """BMI270 any-motion (datasheet 4.8.2, duration 1): |acc - reference| > thr
    on any axis; the reference is updated to the current sample on each
    detection. Returns a bool per sample."""
    a = x[:, :3]; ref = a[0].copy(); out = np.zeros(len(a), bool)
    for i in range(1, len(a)):
        if (np.abs(a[i] - ref) > thr).any():
            out[i] = True; ref = a[i].copy()
    return out

FIFO_FRAMES = 168  # FIFO "full" two frames before 2048 B (datasheet 4.7.5)
def wakes_dense(x, thr=0.25, post_s=0.5, stride=5):
    """Each wake evaluates every window ending in the frames read since the
    previous wake (step `stride`), limited to what the FIFO still holds."""
    slope = anymotion(x, thr)[1:]  # slope[i-1] = detection at sample i
    post = int(post_s * HZ); i = 1; prev_end = 0; out = []
    while True:
        hits = np.nonzero(slope[i - 1:])[0]
        if hits.size == 0: break
        t = i + hits[0]; end = t + post + 1
        if end > len(x): break
        first_new = max(prev_end, end - FIFO_FRAMES)
        if first_new > prev_end: prev_end = first_new  # FIFO overflowed: older frames lost
        for e in range(max(first_new + 1, WIN), end + 1, stride):
            out.append(features(x[e - WIN:e]))
        if end >= WIN and (end - max(first_new + 1, WIN)) % stride: out.append(features(x[end - WIN:end]))
        prev_end = end; i = end + 1
    return out
