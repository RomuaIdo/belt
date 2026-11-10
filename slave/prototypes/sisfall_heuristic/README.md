# SisFall heuristic evaluation

Offline check of the fall gate in `../bmi270_fall_detect.cpp` on the raw
SisFall dataset (38 subjects, sensor at the waist, 200 Hz). The simulator
replays the firmware flow at 50 Hz: any-motion wake (|accel - reference| > 0.25 g
on any axis, reference updated on each detection, as in the BMI270 datasheet 4.8.2),
wait, read, check every 2 s window ending in the new frames.

Raw SisFall (one `.txt` per subject/activity) is on Kaggle as
`kushajm/sisfall-dataset-fall-detection`. `nvnikhil0001/sisfall-enhanced` is
**not** usable here: pre-windowed, normalized, no units or activity labels.

```bash
# 1. raw SisFall -> 50 Hz arrays in g / dps (~1 min)
python3 load_sisfall.py <path>/SisFall_dataset sisfall50.npz
# 2. simulate wakes (thr g, post-trigger s); last arg "dense" = every window per wake
python3 run_eval.py . sisfall50.npz 0.25 1.0 dense_1.0.pkl dense
# 3. grid search, tuned on odd subjects, tested on even ones
python3 tune.py dense_1.0.pkl
# 4. misses / false candidates per activity for peak >= 1.6 g and tilt >= 40 deg
python3 inspect_rule.py dense_1.0.pkl 1.6 40
```

Result used in the firmware (`kImpactG = 1.6`, `kTiltChangeDeg = 40`):
99.9% of 1798 falls pass (missed: 1x F11); 16.0% of ADL recordings pass,
mostly D08-D14 (sitting/lying down quickly). Free fall (|a| <= 0.6 g)
and gyro peak did not improve the gate. Requires numpy.
