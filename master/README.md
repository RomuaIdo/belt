# Master

PlatformIO project using Arduino framework for the ESP32-S3-DevKitC-1
(N16R8: 16 MB flash and 8 MB PSRAM).

## Organization

| Directory | Responsibility |
|---|---|
| `include/App/` and `src/App/` | `AppController`, coordinating Wi-Fi, alert queue, Telegram delivery, and the configuration portal |
| `include/Domain/` and `src/Domain/` | Domain models for configuration, devices, and notifications |
| `include/Messaging/` and `src/Messaging/` | `TelegramTask` (Telegram delivery via FreeRTOS task) and alert message formatting |
| `include/Storage/` and `src/Storage/` | Configuration and call queue persistence in LittleFS |
| `include/Web/` and `src/Web/` | `WebPortal`: configuration web server and API |
| `include/Util/` | Shared utilities, such as MAC address formatting and clock synchronization check |
| `src/main.cpp` | Firmware entry point: `setup()` and `loop()` |
| [frontend/](frontend/) | Configuration web page in HTML, CSS, and JavaScript, stored in LittleFS |
| [test/](test/) | Unity test suites, organized by component |
| [prototypes/](prototypes/) | Independent experiments, excluded from firmware compilation |

Header files (`.h`) reside in `include/` and their implementations (`.cpp`) in `src/`,
using matching subdirectories and base names. For example,
`include/Messaging/TelegramTask.h` corresponds to `src/Messaging/TelegramTask.cpp`.
Include directives use paths relative to `include/`: `Messaging/TelegramTask.h`.
Header-only utilities do not require a `.cpp` file.

## Current status

`AppController` (`src/App/AppController.cpp`):

- `setup()` mounts LittleFS, loads configuration, opens the queue in `/queue`
  (purging invalid files), sets up the master's own Wi-Fi network, starts the web
  server, and connects to saved Wi-Fi (AP+STA mode).
- `execute()`, called by `loop()`, handles configuration page requests, maintains Wi-Fi
  and system clock (NTP, UTC-3 timezone), collects completed sends, and dispatches pending
  alerts to Telegram via FreeRTOS tasks (up to 2 concurrent). The root CA certificate
  is located in `include/Messaging/telegram_certificate.h`.
- `enqueueAlert(mac)` is the entry point for belt alerts: formats the peer message,
  writes it to the queue, and delivery is processed in `execute()`. Network errors and
  5xx/429 HTTP statuses retry with backoff from 5 s to 5 min; HTTP 400/403 drops that chat.

Belt registration is currently performed via the "Add belt manually" button on the web portal.

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

**5. Belts.** On the home screen, tap "Add belt manually" and enter the MAC address. In the form:
ask the user to open the bot in Telegram and press **Start**, then use
**Find conversations** and select who receives alerts. **Send test** dispatches an actual
test message to selected chats; **Save** writes to `config.json`.

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

Other available test suites: `test_alert_message`, `test_domain_models`, `test_notification`,
`test_config_storage`, `test_call_queue_storage`, and `test_board_specs`.
Use the corresponding name with `-f`. Persistence tests write to flash; reboot tests
require a compatible development board and serial connection.

The test environment selects `Domain/`, `Storage/`, and `Messaging/`, without compiling
`src/main.cpp`, `App/`, or `Web/`. Code in `prototypes/` is excluded from all build environments.

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
