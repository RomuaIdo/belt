# Slave (belt)

PlatformIO project using Arduino framework for the ESP32-S3 Super Mini
(ESP32-S3FH4R2: 4 MB flash and 2 MB PSRAM).

The belt pairs with the master, sends an alert to it when the alert button is held,
and shows what it is doing on its LED, buzzer, and vibration motor.

## Organization

| Directory | Responsibility |
|---|---|
| `include/App/` and `src/App/` | `AppController`, coordinating radio, buttons, pairing, alert, IMU, and feedback |
| `include/Alert/` and `src/Alert/` | `AlertService`: state machine for holding the alert button |
| `include/Pairing/` and `src/Pairing/` | `PairingService` (pairing state machine) and `ChannelScanner` (Wi-Fi channel sweep) |
| `include/State/` and `src/State/` | `StateHandler`: system state and event feedback on the outputs (LED, buzzer, vibration) |
| `include/Network/` and `src/Network/` | `EspNowTransceiver`: ESP-NOW send/receive and channel selection |
| `include/Input/` and `src/Input/` | `Button`: debounced button with press edge detection |
| `include/Sensor/` and `src/Sensor/` | `Mpu6050`: IMU driver over I2C |
| `include/Domain/` | `MasterLink` (paired master MAC and channel), `ImuSample`, and `SampleBuffer` (circular buffer) |
| `include/Storage/` and `src/Storage/` | `MasterLinkStorage`: persists the paired master in LittleFS |
| `include/Util/` | `AppConfig.h`: pins, timings, and constants |
| `src/main.cpp` | Firmware entry point: `setup()` and `loop()` |
| [test/](test/) | Unity test suites, organized by component |

The wire protocol shared with the master is in [`../shared/include/Protocol/Message.h`](../shared/include/Protocol/Message.h).

Header files (`.h`) reside in `include/` and their implementations (`.cpp`) in `src/`,
using matching subdirectories and base names. For example,
`include/Pairing/PairingService.h` corresponds to `src/Pairing/PairingService.cpp`.
Include directives use paths relative to `include/`: `Pairing/PairingService.h`.
Header-only files (such as `State/Feedback.h`) do not require a `.cpp` file.

## Current status

`AppController` (`src/App/AppController.cpp`):

- `setup()` mounts LittleFS, puts Wi-Fi in station mode (the belt never joins a network, it
  only needs the radio for ESP-NOW), starts ESP-NOW and the MPU-6050, and restores the
  paired master and its channel from `/master_link.json`.
- `loop()` reads the buttons, advances pairing and alert, processes incoming radio frames,
  reads the IMU, and hands the current state to the `StateHandler`.

**Pairing** (pairing button, GPIO 4). The belt starts it; only the master knows the
router's Wi-Fi channel, so the belt looks for it:

1. The belt scans channels 1 to 13 (300 ms each), sending a broadcast `PairRequest` on each.
2. A master that hears it answers `PairWait`. The belt locks that channel and master and
   keeps repeating the request every 2 s while the caregiver sets the belt up on the
   master's page. If the master goes silent for 10 s, the belt scans again.
3. When the caregiver presses Save, the master sends `PairAccept` with its channel. The
   belt saves the master's MAC and channel, then answers `PairConfirm`. Nothing is saved
   before the `PairAccept`.

The whole attempt times out after 5 minutes.

**Alert** (alert button, GPIO 5). Holding the button for 2 s sends one `Alert` to the paired
master; releasing it re-arms the button. Without a paired master the press is rejected.
The protocol also defines `AlertAck` and resending, which the belt does not use yet.

**IMU.** The MPU-6050 (I2C: SDA GPIO 8, SCL GPIO 9) is read at 100 Hz into a circular buffer
holding the last 3 s. The latest sample is printed on the serial monitor. There is no
fall detection yet.

Not implemented yet: deep sleep (waking up by button or IMU interrupt), `AlertAck`
handling, and fall detection.

## State feedback

`StateHandler` knows what the belt is doing and drives the outputs. It only acts while a
state is active or an event is playing; in `Idle` with no event playing, `update()` does
nothing and `isIdle()` returns true, so the belt can go to sleep.

- A **state** plays for as long as it lasts.
- An **event** is a short feedback played once over the state's, only on the outputs it
  uses; when it ends, those outputs go back to the state's feedback.

| State | Behavior (placeholder values) |
|---|---|
| `Idle` | Everything off |
| `PairingSearching` | LED blinks blue, 150 ms on / 150 ms off |
| `PairingWaiting` | LED blinks blue, 500 ms on / 500 ms off |
| `AlertHolding` | LED steady red |
| `AlertSending` | LED blinks red, 100 ms on / 100 ms off |

| Event | Behavior (placeholder values) |
|---|---|
| `PairingSucceeded` | Two quick beeps and a 400 ms green LED pulse |
| `PairingTimedOut` | One 500 ms beep |
| `AlertSent` | One 400 ms vibration |
| `AlertRejected` | Two short vibrations |
| `BootFault` | LED blinks red three times (radio or IMU failed to start) |

State priority is alert, then pairing, then idle. Every transition is logged on the serial
monitor, for example `StateHandler: Idle -> PairingSearching`.

To change what an output does, edit the tables in
[`include/State/Feedback.h`](include/State/Feedback.h). `PatternPlayer` only computes the
timing of a pattern (off, steady, or blink with N repeats) and does not touch hardware.

## Pins

Defined in `include/Util/AppConfig.h`.

| Function | Pin |
|---|---|
| Pairing button (to GND, internal pull-up) | GPIO 4 |
| Alert button (to GND, internal pull-up) | GPIO 5 |
| MPU-6050 SDA / SCL | GPIO 8 / GPIO 9 |
| Status LED (onboard WS2812 RGB) | GPIO 48 |
| Buzzer | not set (`kNoPin`) |
| Vibration motor | not set (`kNoPin`) |

An output set to `kNoPin` is skipped, so only the LED gives feedback until the buzzer and
motor pins are defined. The buzzer plays at `kBuzzerFreqHz` (2700 Hz).

## Pairing the belt

1. Flash the firmware (below) and open the serial monitor.
2. Set up the master and open its page (see [`../master/README.md`](../master/README.md)).
3. Press the pairing button on the belt. The LED blinks blue while it searches.
4. When the master answers, the LED blinks slower. On the master's page, select the belt,
   fill in the form, and press Save.
5. The belt gives two beeps and a green LED pulse, and the pairing is saved.
6. Hold the alert button for 2 s to send an alert to the master.

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
| `test_alert_service` | Alert hold, single send per hold, release, and abort |
| `test_pairing_service` | Pairing state machine and timeout |
| `test_channel_scanner` | Channel sweep |
| `test_button` | Debounce and press edge |
| `test_master_link_storage` | Paired master persistence (writes to flash) |
| `test_protocol` | Frame building and parsing |
| `test_sample_buffer` | IMU circular buffer |
| `test_board_specs` | Reports the board's flash and PSRAM |
| `test_i2c_scan` | Scans the I2C bus (needs the MPU-6050 wired) |
| `test_mpu6050` | Reads the MPU-6050 (needs the sensor wired) |

The board's flash and PSRAM settings have not been verified on real hardware: run
`test_board_specs` first and read its serial output. If boot is garbled or loops, try
flipping `board_build.flash_mode` between `qio` and `dio` in `platformio.ini`.

The test environment selects `Domain/`, `Storage/`, `Pairing/`, `Input/`, `Sensor/`,
`State/`, and `Alert/`, without compiling `src/main.cpp`, `App/`, or `Network/`.
