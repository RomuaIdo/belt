# Converts raw SisFall (ADXL345 + ITG3200) to 50 Hz arrays in g / dps, cached as one npz.
import sys, os, re, numpy as np
root, out = sys.argv[1], sys.argv[2]
ACC = 2 * 16 / 2**13      # ADXL345 +-16 g, 13 bits -> g per count
GYR = 2 * 2000 / 2**16    # ITG3200 +-2000 dps, 16 bits -> dps per count
names, arrays = [], []
for subj in sorted(os.listdir(root)):
    d = os.path.join(root, subj)
    if not os.path.isdir(d): continue
    for f in sorted(os.listdir(d)):
        m = re.match(r'([DF]\d\d)_(S[AE]\d\d)_R(\d\d)\.txt$', f)
        if not m: continue
        txt = open(os.path.join(d, f)).read().replace(';', ',')
        v = np.array([int(x) for x in txt.replace('\n', '').split(',') if x.strip()], dtype=np.float32)
        v = v[: v.size // 9 * 9].reshape(-1, 9)
        x = np.hstack([v[:, 0:3] * ACC, v[:, 3:6] * GYR])
        n = x.shape[0] // 4 * 4
        x = x[:n].reshape(-1, 4, 6).mean(1)   # 200 -> 50 Hz (block average, like AVG4)
        names.append(f[:-4]); arrays.append(x.astype(np.float32))
lens = np.array([a.shape[0] for a in arrays])
np.savez_compressed(out, names=np.array(names), lens=lens, data=np.concatenate(arrays))
print(len(names), 'recordings,', lens.sum(), 'samples at 50 Hz')
