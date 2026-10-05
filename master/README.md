# Master

PlatformIO project with the Arduino framework for the ESP32-S3-DevKitC-1
(N16R8: 16 MB flash, 8 MB PSRAM).

## Layout

| Directory | Responsibility |
|---|---|
| `include/App/` and `src/App/` | `AppController`, which coordinates Wi-Fi, ESP-NOW, pairing, the alert queue, Telegram sends and the config page |
| `include/Domain/` and `src/Domain/` | Config, device and notification models |
| `include/Messaging/` and `src/Messaging/` | `TelegramTask` (Telegram sends on a FreeRTOS task) and alert message formatting |
| `include/Network/` and `src/Network/` | `EspNowTransceiver` (ESP-NOW radio wrapper) and `PairingService` (dashboard-initiated pairing state machine) |
| `include/Storage/` and `src/Storage/` | Config and queue persistence on LittleFS |
| `include/Web/` and `src/Web/` | `WebPortal`: the config page's web server and the API it uses |
| `include/Util/` | Shared utilities: MAC parsing/formatting and clock validity |
| `src/main.cpp` | Firmware entry point: `setup()` and `loop()` |
| [frontend/](frontend/) | Config page in HTML, CSS and JavaScript, written to LittleFS |
| [test/](test/) | Unity suites, organized by component |
| [prototypes/](prototypes/) | Standalone experiments, outside the firmware build |

`.h` files live in `include/`, their `.cpp` implementations in `src/`, under the
same subdirectory and base name — e.g. `include/Messaging/TelegramTask.h`
pairs with `src/Messaging/TelegramTask.cpp`. Includes use the path from
`include/`: `Messaging/TelegramTask.h`. Header-only utilities skip the `.cpp`.

## Current state

`AppController` (`src/App/AppController.cpp`):

- `setup()` mounts LittleFS, loads the config, opens the `/queue` directory
  (purging invalid files), brings up the master's own Wi-Fi network, starts
  ESP-NOW, and connects to the saved Wi-Fi (AP+STA mode).
- `execute()`, called from `loop()`, services the config page, maintains
  Wi-Fi and the clock (NTP, UTC-3), drains the ESP-NOW alert queue, ticks
  the pairing state machine, collects finished sends and dispatches pending
  ones to Telegram on FreeRTOS tasks (at most 2 at a time). The root
  certificate lives in `include/Messaging/telegram_certificate.h`.
- `enqueueAlert(mac)` is the entry point for a belt's alert: it formats the
  peer's message, writes it to the queue, and the send happens in
  `execute()`. Network/5xx/429 failures retry with a 5s-to-5min backoff;
  HTTP 400/403 drops that chat.
- Pairing has two symmetric modes, both completing in `handlePairingMessage()`:
  a belt broadcasting unprompted (mode 1) gets an immediate `PairResponse`
  and lands in a pending list surfaced by `GET /api/pendentes`, finalized
  once the caregiver fills in its name/message/recipients; a MAC typed
  directly into the dashboard (mode 2) triggers a bounded, blocking
  handshake (`PUT /api/dispositivos/{mac}`) before it's saved.

## Setting up the master from a phone

Config (Wi-Fi, Telegram token, belts) lives in LittleFS's `/config.json` and
loads on every boot: once configured, the master reconnects on its own.

**1. Flash the page and the firmware** (board on the `UART`-labeled USB port; from `master/`):

```sh
pio run -e esp32-s3-devkitc-1 -t uploadfs   # writes frontend/ to LittleFS
pio run -e esp32-s3-devkitc-1 -t upload     # writes the firmware
pio device monitor                           # serial at 115200
```

> `uploadfs` **wipes the entire LittleFS**, including `config.json` and the
> alert queue. Only run it when `frontend/` changes; use `upload` alone for
> firmware-only changes.

The monitor should print `network 'CintoAlerta-Master' up; page at http://192.168.4.1`.

**2. Open the page.** On the phone, connect to the `CintoAlerta-Master`
Wi-Fi network (password `cintoalerta`, set in `src/App/AppController.cpp`)
and open <http://192.168.4.1>. The master's own network stays up even after
it connects to the router. Without Wi-Fi or a token, the page opens
straight into **Settings**.

**3. Wi-Fi.** In Settings, "Search networks", pick the home network's
**2.4 GHz** band (the ESP32 doesn't use 5 GHz), enter the password and
"Connect". The phone may lose the master's signal for a second or two while
its channel changes.

**4. Telegram.** On Telegram, message `@BotFather`, send `/newbot` and copy
the token. On the page, paste the token, "Test" (the master queries Telegram
and shows the bot's name) and "Save".

**5. Belts.** On the home screen, press the pairing button on a belt (it
shows up under "Waiting to be set up") or use "Add belt manually" and type
its MAC. In the form: ask the person to open the bot on Telegram and press
**Start**, then use **Find conversations** and tick who should receive
alerts. **Send test** sends a real message to those ticked; **Save** writes
to `config.json`.

If someone doesn't show up in "Find conversations", Telegram only keeps
messages for 24h: ask them to message the bot again.

## Build and test

Run from `master/`, in a terminal with PlatformIO available.

```sh
# Full firmware build.
pio run -e esp32-s3-devkitc-1

# Compile a suite without uploading or running it on a board.
pio test -e esp32-s3-devkitc-1-test -f test_telegram_task --without-uploading --without-testing

# Upload and run a suite on the connected board.
pio test -e esp32-s3-devkitc-1-test -f test_telegram_task
```

The other suites are `test_alert_message`, `test_domain_models`,
`test_notification`, `test_config_storage`, `test_call_queue_storage`,
`test_pairing_service`, `test_protocol` and `test_board_specs`. Use the
matching name with `-f`. Persistence suites write to flash; reboot tests
need a board with a compatible serial connection.

The test environment selects `Domain/`, `Storage/`, `Messaging/` and
`Network/`, without compiling `src/main.cpp`, `App/` or `Web/`. Code under
`prototypes/` is excluded from every environment.

## Previewing the frontend without a board

From `master/`:

```sh
python -m http.server 8000 --bind 127.0.0.1 --directory frontend
```

Open <http://localhost:8000/?mock>. The `?mock` loads `mock.js`, which fakes
the firmware's API (`src/Web/WebPortal.cpp`) without an ESP32 or real
Telegram calls. Without `?mock`, the page talks to the real API. The master
only serves `index.html`, `style.css` and `app.js`; `mock.js` is written to
flash along with the rest of the folder, but never served.

This local preview never writes any configuration to the master.
