# Fall-confirmation model

Small 1D CNN that confirms the fall candidates found by the heuristic gate in
`../fall_pipeline.h`. Both run on the ESP32-S3 inside
`../bmi270_fall_detect.cpp`: any-motion wakes the ESP, every 2 s window in the
new FIFO data goes through the heuristic, and the passing window with the
strongest impact goes to the model.

## Model

- Input: 2 s window, 50 Hz (100 frames). Four **orientation-invariant**
  channels, so it does not depend on how the sensor sits on the belt (SisFall's
  sensor and ours are mounted differently):
  |a|, a along the gravity at the start of the window, a across it, |gyro|.
- `conv(4->8,k5) relu pool2 -> conv(8->16,k5) relu pool2 -> conv(16->16,k5) relu -> mean -> dense(16->1) -> sigmoid`
- 2137 float weights (~8.5 KB flash), ~67k multiply-adds, ~11.6 KB static RAM
  for activations. Plain C++ in `fall_pipeline.h`, no ML library.
- Fall when probability >= 0.5 (chosen on the validation subjects).

## Data and results

Raw SisFall (38 subjects, sensor at the waist), resampled to 50 Hz and run
through a simulation of the firmware flow; the model is trained only on the
windows the firmware would hand it. Split by subject: train / validation
(SA05, SA10, SA15, SA20, SE05, SE10, SE15) / test (SA03, SA08, SA13, SA18,
SA23, SE03, SE08, SE13).

Per recording on the **test** subjects (alert = any wake confirmed):

| | Falls alerted | ADL recordings alerted |
|---|---|---|
| Heuristic only | 100% | 21.0% |
| Heuristic + model (accel + gyro, used) | 100% | 1.4% |
| Heuristic + model (accel only) | 99.5% | 1.6% |

The committed weights come from the run in the table. GPU training is not
bit-exact: a rerun of `train_fall_model.py` gave 100% / 1.6% at the same threshold.

SisFall ADLs are dense, deliberate activities (jumping, sitting/lying down
quickly), so real-life false-alarm rates per hour should be lower. The
accel-only variant is nearly as good: worth it if the gyro is turned off to
save power (BMI270: 685 uA accel+gyro vs tens of uA accel-only low power).

## Files

- `train_fall_model.py`: SisFall download, simulation, training, threshold,
  writes `fall_model_weights.b64` + `model_meta.json` (weights, threshold and
  golden windows: name, window end, expected heuristic values and probability).
- `simulate_schedule.py <path>/SisFall_dataset .`: old vs new wake schedule
  (any-motion + 1 s awake vs timer reads while active, idle after 3/5/10 s):
  same detection (100% / 1.4%), ESP awake ~1.4% instead of ~52% while moving.
- `export_model.py <path>/SisFall_dataset`: checks the weights reproduce the
  golden probabilities, then generates `../fall_model_weights.h` and
  `../../test/test_fall_pipeline/golden.h`.

## Retraining

```bash
python3 train_fall_model.py          # Colab GPU: a few minutes
python3 export_model.py <SisFall_dataset dir>
cd ../.. && pio test -e esp32-s3-supermini-test -f test_fall_pipeline
```
