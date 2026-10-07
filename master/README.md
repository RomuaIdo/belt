# Master

PlatformIO project with the Arduino framework for the ESP32-S3-DevKitC-1
(N16R8: 16 MB flash, 8 MB PSRAM).

## Layout

| Directory | Responsibility |
|---|---|
| `include/App/` and `src/App/` | `AppController`, which coordinates Wi-Fi, ESP-NOW, pairing, the alert queue, Telegram sends and the config page |
| `include/Domain/` and `src/Domain/` | Config, device and notification models, and `PendingPairingList` (belts waiting to be set up) |
| `include/Messaging/` and `src/Messaging/` | `TelegramTask` (Telegram sends on a FreeRTOS task) and alert message formatting |
| `include/Network/` and `src/Network/` | `EspNowTransceiver` (ESP-NOW radio wrapper: the receive callback only queues frames) and `PairingService` (waits for a belt's confirmation while saving it) |
| `include/Storage/` and `src/Storage/` | Config and queue persistence on LittleFS |
| `include/Web/` and `src/Web/` | `WebPortal`: the config page's web server and the API it uses |
| `include/Util/` | Clock validity. MAC parsing/formatting and the wire protocol (`Protocol/Message.h`) live in [../shared/](../shared/include), shared with the slave |
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
  Wi-Fi and the clock (NTP, UTC-3), handles the ESP-NOW frames received since
  the last step, collects finished sends and dispatches pending ones to
  Telegram on FreeRTOS tasks (at most 2 at a time). The root
  certificate lives in `include/Messaging/telegram_certificate.h`.
- `enqueueAlert(mac)` is the entry point for a belt's alert: it formats the
  peer's message, writes it to the queue, and the send happens in
  `execute()`. Network/5xx/429 failures retry with a 5s-to-5min backoff;
  HTTP 400/403 drops that chat.
- Radio receive path: ESP-NOW calls back on the Wi-Fi task (a FreeRTOS task, not
  a hardware interrupt), so `EspNowTransceiver` only copies each frame into a
  queue. `execute()` drains it oldest first on the `loop()` task, where touching
  the config and LittleFS is safe. Frames without the protocol's magic, a known
  type and the exact size are dropped silently (other ESP-NOW traffic).
- Alerts: an `Alert` from a registered belt is appended to the pending list
  (`enqueueAlert()`), then acknowledged with `AlertAck` echoing its `seq`. A
  resent alert (same `seq`) is acknowledged but not queued twice. An alert from an
  unregistered MAC is dropped without an acknowledgement.
- Pairing is always started by the belt (details in the
  [root README](../README.md) and `shared/include/Protocol/Message.h`):
  - a `PairRequest` from an unregistered MAC lists the belt under "Waiting to be
    set up" (`GET /api/pendentes`) and gets `PairWait`; it leaves the list 15 s
    after its last request (a belt repeats every 2 s), and at most 8 are listed;
  - **Save** (`PUT /api/dispositivos/{mac}`) sends `PairAccept` with the master's
    channel, resends it every 0.5 s and waits up to 1.5 s for the belt's
    `PairConfirm`. Only then is the belt saved to `config.json`; otherwise nothing
    is saved and the page shows "The belt did not answer" (HTTP 504);
  - a `PairRequest` from an already registered MAC is accepted right away, which
    lets a belt that lost the channel or its memory rejoin without the page.

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
its channel changes. Once connected, any device on the same home network can
open the page at <http://cintoalerta.local> (or the IP shown in Settings),
without using the master's weaker own network. If `.local` doesn't resolve
(older Android, guest networks), use the IP.

**4. Telegram.** On Telegram, message `@BotFather`, send `/newbot` and copy
the token. On the page, paste the token, "Test" (the master queries Telegram
and shows the bot's name) and "Save".

**5. Belts.** Press the pairing button on a belt: it shows up on the home screen
under "Waiting to be set up" (it keeps asking for up to 5 minutes). Choose "Set
up". In the form: ask the person to open the bot on Telegram and press
**Start**, then use **Find conversations** and tick who should receive
alerts. **Send test** sends a real message to those ticked; **Save** pairs the
belt (the master waits ~1.5 s for its confirmation) and writes to `config.json`.
If the page says "The belt did not answer", press the belt's pairing button
again and Save once more.

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
`test_pairing_service`, `test_pending_pairing_list`, `test_protocol` (the shared
wire protocol) and `test_board_specs`. Use the matching name with `-f`. Persistence suites write to flash; reboot tests
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
