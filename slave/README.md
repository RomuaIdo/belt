# Slave (belt)

PlatformIO project using Arduino framework for the ESP32-S3 Super Mini
(ESP32-S3FH4R2: 4 MB flash and 2 MB PSRAM).

The belt pairs with the master, sends an alert to it when the alert button is clicked
or when it detects a fall, and shows what it is doing on its LED, buzzer, and vibration
motor.

## Organization

| Directory | Responsibility |
|---|---|
| `include/App/` and `src/App/` | `AppController`, coordinating radio, buttons, pairing, alert, IMU, fall detection, and feedback |
| `include/Fall/` and `src/Fall/` | `FallPipeline.h` (heuristic + CNN, weights in `FallModelWeights.h`), `FallDetector` (FIFO reads to fall decisions), and `FallService` (cancel countdown) |
| `include/Pairing/` and `src/Pairing/` | `PairingService` (pairing state machine) and `ChannelScanner` (Wi-Fi channel sweep) |
| `include/State/` and `src/State/` | `StateHandler`: system state and event feedback on the outputs (LED, buzzer, vibration) |
| `include/Network/` and `src/Network/` | `EspNowTransceiver`: ESP-NOW send/receive and channel selection |
| `include/Input/` and `src/Input/` | `Button`: debounced button with press edge detection |
| `include/Sensor/` and `src/Sensor/` | `Bmi270`: IMU driver over SPI (FIFO and any-motion) |
| `include/Domain/` | `MasterLink` (paired master MAC and channel) |
| `include/Storage/` and `src/Storage/` | `MasterLinkStorage`: persists the paired master in LittleFS |
| `include/Util/` | `AppConfig.h`: pins, timings, and constants |
| `src/main.cpp` | Firmware entry point: `setup()` and `loop()` |
| [test/](test/) | Unity test suites, organized by component |
| [prototypes/](prototypes/) | Standalone sketches and the training of the fall model ([`prototypes/fall_model/README.md`](prototypes/fall_model/README.md)) |

The wire protocol shared with the master is in [`../shared/include/Protocol/Message.h`](../shared/include/Protocol/Message.h).

Header files (`.h`) reside in `include/` and their implementations (`.cpp`) in `src/`,
using matching subdirectories and base names. For example,
`include/Pairing/PairingService.h` corresponds to `src/Pairing/PairingService.cpp`.
Include directives use paths relative to `include/`: `Pairing/PairingService.h`.
Header-only files (such as `State/Feedback.h`) do not require a `.cpp` file.

## Current status

`AppController` (`src/App/AppController.cpp`):

- `setup()` mounts LittleFS, puts Wi-Fi in station mode (the belt never joins a network, it
  only needs the radio for ESP-NOW), starts ESP-NOW and the BMI270, and restores the
  paired master and its channel from `/master_link.json`.
- `loop()` reads the buttons, checks the IMU for falls, advances pairing and the fall
  countdown, processes incoming radio frames, and hands the current state to the
  `StateHandler`.

**Pairing** (pairing button, GPIO 2). The belt starts it; only the master knows the
router's Wi-Fi channel, so the belt looks for it:

1. The belt scans channels 1 to 13 (300 ms each), sending a broadcast `PairRequest` on each.
2. A master that hears it answers `PairWait`. The belt locks that channel and master and
   keeps repeating the request every 2 s while the caregiver sets the belt up on the
   master's page. If the master goes silent for 10 s, the belt scans again.
3. When the caregiver presses Save, the master sends `PairAccept` with its channel. The
   belt saves the master's MAC and channel, then answers `PairConfirm`. Nothing is saved
   before the `PairAccept`.

The whole attempt times out after 5 minutes.

**Alert** (alert button, GPIO 7). One click sends an `Alert` to the paired master. Without
a paired master the click is rejected. During a fall countdown the click cancels the fall
instead. After sending, the belt stays awake in `AlertSending` and resends the `Alert` with
the same seq every 500 ms until the master's `AlertAck` arrives (`AlertSent`), for up to 10
attempts (`AlertFailed`). The master acknowledges every copy but queues the call once.

**Fall detection.** The BMI270 (SPI) samples accel and gyro at 50 Hz into its FIFO, which
holds the last ~3.4 s.

1. While still, the belt only watches INT1: the BMI270's any-motion (~0.25 g), latched.
2. On movement it reads the FIFO at once (the frames before the movement are already
   there), then every 1 s. After 5 s without movement it goes back to watching INT1.
3. Every 2 s window ending in the new frames goes through a cheap heuristic (impact
   >= 1.6 g and the body ending >= 40 degrees from where it started). The passing window
   with the strongest impact goes to a small CNN that confirms the fall. Model, training,
   and SisFall results: [`prototypes/fall_model/README.md`](prototypes/fall_model/README.md).
4. A confirmed fall starts `FallCountdown` (5 s): LED, buzzer, and vibration go off so the
   wearer notices. A click on the alert button cancels it (false positive). Otherwise the
   `Alert` goes to the master when the countdown ends, with the same ack flow as a click.

**Power.** When nothing is happening, the ESP goes into light sleep, as in
`prototypes/bmi270_fall_detect.cpp`. Nothing is happening when the `StateHandler` is idle,
no pairing is running, no alert is waiting for its ack, and both buttons are released.

- While still, it sleeps until any-motion raises INT1.
- While moving, it sleeps until the next 1 s FIFO read. INT1 stays latched while moving,
  so a timer is used then.
- Either button also wakes it.
- A timer wakes it every 30 min for the battery reading.

**Battery.** The LiPo (3.7 V, 4.2 V full, 500 mAh) goes through a 1:2 divider into GPIO 8
(ADC1, works with the radio on). `BatteryMonitor` averages 16 ADC readings at boot, every
30 min, on every button click (before the radio sends) and on every confirmed fall. Below
3.5 V the belt enters `BatteryLow` and beeps three times (200 ms on / 200 ms off) on every
low reading; it leaves the state only above 3.65 V. `BatteryLow` is quiet, so the belt keeps
sleeping. Thresholds and interval are in `AppConfig` (`kBattery*`). Every reading is logged
with an estimated charge from a LiPo discharge curve (`BatteryMonitor::percentFromMv`), for
example `AppController: battery 3.91 V, 68%`, or `... LOW` while the battery is low.

The S3 USB serial drops during light sleep: set `AppConfig::kUseLightSleep = false` to
debug over USB.

## State feedback

`StateHandler` knows what the belt is doing and drives the outputs. It only acts while a
state is active or an event is playing; in `Idle` with no event playing, `update()` does
nothing and `isIdle()` returns true (the same in `BatteryLow`, which has no feedback of its own), so the belt can go to sleep.

- A **state** plays for as long as it lasts.
- An **event** is a short feedback played once over the state's, only on the outputs it
  uses; when it ends, those outputs go back to the state's feedback.

| State | Behavior (placeholder values) |
|---|---|
| `Idle` | Everything off |
| `PairingSearching` | LED blinks blue, 150 ms on / 150 ms off |
| `PairingWaiting` | LED blinks blue, 500 ms on / 500 ms off |
| `AlertSending` | LED blinks red, 100 ms on / 100 ms off (waiting for the `AlertAck`) |
| `FallCountdown` | LED blinks red 200/200 ms, buzzer beeps 200 ms every 1 s, motor vibrates 500/500 ms |
| `BatteryLow` | Everything off (the `BatteryLow` event warns); the belt still sleeps |

| Event | Behavior (placeholder values) |
|---|---|
| `PairingSucceeded` | Two quick beeps and a 400 ms green LED pulse |
| `PairingTimedOut` | One 500 ms beep |
| `AlertSent` | One 400 ms vibration and red LED pulse (master acknowledged) |
| `AlertRejected` | Two short vibrations |
| `AlertFailed` | Three beeps and red LED blinks (no `AlertAck` after every retry) |
| `FallCancelled` | One 400 ms green LED pulse |
| `BootFault` | LED blinks red three times (radio or IMU failed to start) |
| `BatteryLow` | Three 200 ms beeps, 200 ms apart (on every low battery reading) |

State priority is fall countdown, then alert, then pairing, then battery low, then idle. Every transition is
logged on the serial monitor, for example `StateHandler: Idle -> PairingSearching`.

To change what an output does, edit the tables in
[`include/State/Feedback.h`](include/State/Feedback.h). `PatternPlayer` only computes the
timing of a pattern (off, steady, or blink with N repeats) and does not touch hardware.

## Pins

Defined in `include/Util/AppConfig.h`.

| Function | Pin |
|---|---|
| Pairing button (to GND, internal pull-up) | GPIO 2 |
| Alert button (to GND, internal pull-up) | GPIO 7 |
| BMI270 CS / MOSI (SDA) / SCK (SCL) / MISO (SDO) | GPIO 10 / 12 / 13 / 11 |
| BMI270 INT1 | GPIO 9 |
| Status LED (onboard WS2812 RGB) | GPIO 48 |
| Buzzer | GPIO 1 |
| Vibration motor | GPIO 6 |
| Battery (1:2 divider, ADC1_CH7) | GPIO 8 |

An output set to `kNoPin` is skipped. The buzzer plays at `kBuzzerFreqHz` (2700 Hz). The
BMI270 starts in I2C mode and switches to SPI on the first CS rising edge.

## Pairing the belt

1. Flash the firmware (below) and open the serial monitor.
2. Set up the master and open its page (see [`../master/README.md`](../master/README.md)).
3. Press the pairing button on the belt. The LED blinks blue while it searches.
4. When the master answers, the LED blinks slower. On the master's page, select the belt,
   fill in the form, and press Save.
5. The belt gives two beeps and a green LED pulse, and the pairing is saved.
6. Click the alert button to send an alert to the master.

The pairing survives reboots: it is stored in `/master_link.json` on LittleFS.

## Building and testing

Run from `slave/` in a terminal with PlatformIO installed. The board uses native USB
(CDC), so connect the USB port of the Super Mini.

```sh
# Full firmware compilation
pio run -e esp32-s3-supermini

# Flash the firmware and open the serial monitor (115200 baud)
pio run -e esp32-s3-supermini -t upload
pio device monitor

# Compile a test suite without flashing or running on board
pio test -e esp32-s3-supermini-test -f test_state_handler --without-uploading --without-testing

# Flash and execute a test suite on a connected board
pio test -e esp32-s3-supermini-test -f test_state_handler
```

Available test suites:

| Suite | Covers |
|---|---|
| `test_pattern_player` | Blink timing, repeats, and clock rollover |
| `test_state_handler` | State priority, events over states, and `isIdle()` (no pins used) |
| `test_fall_pipeline` | Heuristic and CNN against the Python results on SisFall windows |
| `test_fall_detector` | Windows across FIFO reads, lost frames, one report per fall |
| `test_fall_service` | Fall countdown, cancel, single send |
| `test_pairing_service` | Pairing state machine and timeout |
| `test_channel_scanner` | Channel sweep |
| `test_button` | Debounce and press edge |
| `test_master_link_storage` | Paired master persistence (writes to flash) |
| `test_protocol` | Frame building and parsing |
| `test_board_specs` | Reports the board's flash and PSRAM |

The board's flash and PSRAM settings have not been verified on real hardware: run
`test_board_specs` first and read its serial output. If boot is garbled or loops, try
flipping `board_build.flash_mode` between `qio` and `dio` in `platformio.ini`.

The test environment selects `Domain/`, `Storage/`, `Pairing/`, `Input/`, `Sensor/`,
`State/`, and `Fall/`, without compiling `src/main.cpp`, `App/`, or `Network/`.

### Prototypes

Each prototype has its own environment that builds only that file, none of `src/`:

```sh
# BMI270 fall detection with light sleep (the flow the firmware is based on)
pio run -e proto-bmi270-fall -t upload -t monitor

# MPU-6050 FIFO alignment test (kept to document why the MPU-6050 was replaced)
pio run -e proto-mpu-fifo -t upload -t monitor
```

The model is trained in Python from SisFall; `prototypes/fall_model/export_model.py`
regenerates `include/Fall/FallModelWeights.h` and the golden windows used by
`test_fall_pipeline`.
