# Master

PlatformIO project using Arduino framework for the ESP32-S3-DevKitC-1
(N16R8: 16 MB flash and 8 MB PSRAM).

## Organization

| Directory | Responsibility |
|---|---|
| `include/App/` and `src/App/` | `AppController`, coordinating Wi-Fi, ESP-NOW, pairing, alert queue, Telegram delivery, and the configuration portal |
| `include/Domain/` and `src/Domain/` | Domain models for configuration, devices (`PeerNode`), notifications, and belts waiting to be set up (`PendingPairingList`) |
| `include/Network/` and `src/Network/` | `EspNowTransceiver` (ESP-NOW send/receive), `PairingService` (waits for a belt's `PairConfirm`), and `WifiPolicy` (classifies Wi-Fi disconnect reasons) |
| `include/Messaging/` and `src/Messaging/` | `TelegramTask` (Telegram delivery via FreeRTOS task) and alert message formatting |
| `include/Storage/` and `src/Storage/` | Configuration and call queue persistence in LittleFS |
| `include/Web/` and `src/Web/` | `WebPortal`: configuration web server and API |
| `include/Util/` | `StatusLed` (onboard RGB LED showing the Wi-Fi state) and the clock synchronization check |
| `src/main.cpp` | Firmware entry point: `setup()` and `loop()` |
| [frontend/](frontend/) | Configuration web page in HTML, CSS, and JavaScript, stored in LittleFS |
| [test/](test/) | Unity test suites, organized by component |
| [prototypes/](prototypes/) | Independent experiments, excluded from firmware compilation |

The wire protocol shared with the belt is in [`../shared/include/Protocol/Message.h`](../shared/include/Protocol/Message.h),
and MAC address helpers in [`../shared/include/Util/MacUtils.h`](../shared/include/Util/MacUtils.h).

Header files (`.h`) reside in `include/` and their implementations (`.cpp`) in `src/`,
using matching subdirectories and base names. For example,
`include/Messaging/TelegramTask.h` corresponds to `src/Messaging/TelegramTask.cpp`.
Include directives use paths relative to `include/`: `Messaging/TelegramTask.h`.
Header-only utilities do not require a `.cpp` file.

## Current status

`AppController` (`src/App/AppController.cpp`):

- `setup()` mounts LittleFS, loads configuration, opens the queue in `/queue`
  (purging invalid files), sets up the master's own Wi-Fi network, starts the web
  server and mDNS, starts ESP-NOW, and connects to saved Wi-Fi (AP+STA mode).
- `execute()`, called by `loop()`, handles configuration page requests, handles the
  ESP-NOW frames from the belts, maintains Wi-Fi and the system clock (NTP, UTC-3
  timezone), collects completed sends, and dispatches pending alerts to Telegram via
  FreeRTOS tasks (up to 2 concurrent). The root CA certificate is located in
  `include/Messaging/telegram_certificate.h`.
- `enqueueAlert(mac)` is the entry point for belt alerts: formats the peer message,
  writes it to the queue, and delivery is processed in `execute()`. Network errors and
  5xx/429 HTTP statuses retry with backoff from 5 s to 5 min; HTTP 400/403 drops that chat.

**Wi-Fi.** The master scans for the saved network and connects only when it is nearby
(re-scanning every 30 s while disconnected). A hidden network is not listed by name, so
if the scan finds any hidden network the master tries the saved one anyway. A few quick
retries absorb flaky disconnects, and an attempt with no answer in 20 s is dropped. A
password the router rejects, or 3 handshake timeouts in a row (a single one also happens
with the right password, e.g. while the router reboots), counts as a wrong password: the
page reports it and the master retries only every 5 min until the credentials change.
The page can also forget the saved network. On the home network the page
is at <http://cintoalerta.local> (mDNS) or at the IP shown on the serial monitor.

**Status LED** (onboard RGB, GPIO 48 on DevKitC-1 v1.0): dim red when disconnected,
breathing blue while connecting, dim green when connected to the home Wi-Fi.

**Belts (ESP-NOW).** The belt always starts the conversation (see
[`../slave/README.md`](../slave/README.md)):

- `PairRequest` from a new belt lists it under **Waiting to be set up** on the page (it
  leaves the list after 15 s of silence) and is answered with `PairWait`. A belt that is
  already registered gets a `PairAccept` right away.
- When the caregiver saves the belt's form, the master sends `PairAccept` with its Wi-Fi
  channel every 500 ms and saves the belt only after its `PairConfirm` (1.5 s timeout).
- `Alert` from a registered belt is queued and answered with `AlertAck`, echoing the seq.
  The belt resends until it gets the ack; a resent alert (same seq) is acknowledged but
  not queued again. Alerts from unknown belts are not acknowledged.

## Setting up the master via smartphone

Configuration (Wi-Fi, Telegram token, belts) is stored in `/config.json` on LittleFS and
loaded on every boot: once configured, the master reconnects automatically.

**1. Flash the web portal and firmware** (board connected to USB port marked `UART`; from `master/`):

```sh
pio run -e esp32-s3-devkitc-1 -t uploadfs   # writes frontend/ to LittleFS
pio run -e esp32-s3-devkitc-1 -t upload     # flashes firmware
pio device monitor                           # serial monitor at 115200 baud
```

> `uploadfs` **erases the entire LittleFS partition**, including `config.json` and the alert queue.
> Run only when `frontend/` changes; to update only firmware, use `upload`.

The serial monitor should display `rede 'CintoAlerta-Master' criada; pagina em http://192.168.4.1`.

**2. Open the page.** On your smartphone, connect to the Wi-Fi network `CintoAlerta-Master`
(password `cintoalerta`, defined in `src/App/AppController.cpp`) and open
<http://192.168.4.1>. The master's AP remains active even after connecting to the home router.
Without Wi-Fi or bot token configured, the page opens directly on **Settings**.

**3. Wi-Fi.** In Settings, tap "Search networks", select the home **2.4 GHz** network
(the ESP32 does not support 5 GHz), enter the password, and tap "Connect". The phone may lose
connection to the master for 1-2 seconds when the Wi-Fi channel switches.

**4. Telegram.** In Telegram, open `@BotFather`, send `/newbot`, and copy the token.
On the web page, paste the token, tap "Test" (the master queries Telegram and displays the bot username), and tap "Save".

**5. Belts.** Press the pairing button on the belt: within a few seconds it appears under
**Waiting to be set up** on the home screen. Open it and, in the form, ask the user to open
the bot in Telegram and press **Start**, then use **Find conversations** and select who
receives alerts. **Send test** dispatches an actual test message to selected chats;
**Save** pairs the belt and writes it to `config.json`.

If a chat does not appear in "Find conversations", Telegram retains update history for
up to 24 hours: ask the person to send a new message to the bot.

## Building and testing

Run from `master/` in a terminal with PlatformIO installed.

```sh
# Full firmware compilation
pio run -e esp32-s3-devkitc-1

# Compile Telegram test suite without flashing or running on board
pio test -e esp32-s3-devkitc-1-test -f test_telegram_task --without-uploading --without-testing

# Flash and execute a test suite on a connected board
pio test -e esp32-s3-devkitc-1-test -f test_telegram_task
```

Available test suites:

| Suite | Covers |
|---|---|
| `test_telegram_task` | Telegram request payload and input checks (no network) |
| `test_alert_message` | Alert message formatting |
| `test_domain_models` | Configuration and device models |
| `test_notification` | Notification state |
| `test_pending_pairing_list` | Belts waiting to be set up, expiry, and capacity |
| `test_pairing_service` | Wait for the belt's `PairConfirm` and its timeout |
| `test_protocol` | Frame building and parsing |
| `test_wifi_policy` | Wi-Fi disconnect reason classification and wrong-password detection |
| `test_status_led` | LED breathing curve |
| `test_config_storage` | Configuration persistence across a reboot (writes to flash) |
| `test_call_queue_storage` | Alert queue persistence across a reboot, with power-loss simulation |
| `test_board_specs` | Reports the board's flash and PSRAM |

Use the corresponding name with `-f`. Persistence tests write to flash; reboot tests
require a compatible development board and serial connection.

The test environment selects `Domain/`, `Storage/`, `Messaging/`, and `Network/`, without
compiling `src/main.cpp`, `App/`, or `Web/`. Code in `prototypes/` is excluded from all
build environments.

## Previewing the frontend without hardware

From `master/`:

```sh
python -m http.server 8000 --bind 127.0.0.1 --directory frontend
```

Open <http://localhost:8000/?mock>. The `?mock` parameter loads `mock.js`, simulating the firmware
API (`src/Web/WebPortal.cpp`) without an ESP32 or actual Telegram calls. Without `?mock`,
the page connects to the live firmware API. The master serves only `index.html`, `style.css`,
and `app.js`; `mock.js` is stored on flash along with the folder, but not served.

This local preview does not persist configuration to the master.
