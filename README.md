# Cinto Alerta (Fall Alert Belt)

Fall detector and notifier for elderly people. Two belts with an ESP32-S3 and an MPU-6050
classify falls with an embedded model, warn the wearer with vibration and an LED and — if the
wearer doesn't cancel — send an alert via ESP-NOW to a master station, which notifies a
caregiver over Telegram.

Academic project for Integration Workshop 2 — Computer Engineering, UTFPR.
Team: Rafael de Andrade Fernandes, Arthur Gabriel Pellegrini Heberle, Vinícius Romualdo Silva.

## Architecture

```
2x BELT (slave)              1x MASTER                     NOTIFICATION
ESP32-S3 Supermini          ESP32-S3 Dev Module           Telegram Bot API
MPU-6050                    plugged into an outlet
LiPo 500 mAh       ESP-NOW            Wi-Fi / HTTPS
vibration motor  ───────►             ──────────►           caregiver's phone
button(s)         (MAC, no IP)        (REST + JSON)
```

Each belt is identified by its **MAC address** — there is no IP between belt and master.

## Repository layout

| Path | Contents |
|---|---|
| `master/` | Master firmware — PlatformIO project (`include/`, `src/`, `test/`) targeting an ESP32-S3-DevKitC-1 (N16R8: 16 MB flash, 8 MB PSRAM) |
| `master/prompt.md` | Master's class diagram and per-class design notes (architecture source of truth) |
| `master/prototypes/` | Work in progress from other team members, not yet wired into the firmware above (see below) |
| `slave/` | Belt (slave) firmware — PlatformIO project targeting an ESP32-S3 Super Mini (ESP32-S3FH4R2: 4 MB flash, 2 MB PSRAM) |
| `slave/prompt.md` | Slave's class diagram, per-class design notes, and the task-by-task roadmap for the rest of the belt firmware |
| `shared/` | Wire-protocol and MAC-utility headers included by both `master/` and `slave/` (`-I../shared/include`), so they can never drift out of sync between the two firmwares |
| `docs/Plano_de_Projeto.pdf` | Project plan submitted for the course |

## Master firmware (`master/`)

PlatformIO project (Arduino framework) implementing the master station: SoftAP + captive
portal for setup, a config/monitoring web dashboard, ESP-NOW pairing and reception with an
immediate ACK, Telegram notifications, and a power-loss-safe retry queue on LittleFS. The
domain model, storage, network and web layers are covered by the Unity test suites under
`master/test/`.

## Slave firmware (`slave/`)

PlatformIO project (Arduino framework) for the belt. Implemented so far: a pairing button that
listens for the master's broadcast to learn its MAC and replies so the master learns the belt's
MAC back (bidirectional ESP-NOW addressing, no manual MAC entry on either side), with the
learned master MAC persisted on LittleFS across reboots. Motion sensing/fall detection,
vibration+LED warning with a cancel window, alert transmission, and battery monitoring are not
implemented yet — see `slave/prompt.md`'s roadmap section for the planned, team-dividable tasks.

## Work in progress (`master/prototypes/`)

Prototype code contributed by other team members that **is not yet connected** to the firmware
above — treat it as a separate, standalone exploration until it gets wired in:

- `frontend/` — master configuration UI mockup (plain HTML/CSS/JS, meant to eventually be
  served from LittleFS). `app.js` starts with `const MOCK = true`: every call is served by
  `mock.js` (fake data, simulated latency). Run it locally with:
  ```bash
  cd master/prototypes/frontend
  python -m http.server 8000    # or: python3 -m http.server 8000
  # open http://localhost:8000
  ```
- `telegram_call.cpp` — standalone prototype for the Telegram Bot API HTTP call, independent
  from `master/src/Messaging/TelegramNotifier.cpp`.

## Schedule

| Milestone | Theme | Deadline |
|---|---|---|
| M1 | Web infrastructure and communication | Oct 07, 2026 |
| M2 | Electronics, power and machine learning | Nov 11, 2026 |
| M3 | Prototyping and final tests | Nov 25, 2026 |
| M4 | Final submission (report, video, blog) | Dec 02, 2026 |
| M5 | Committee presentation | Dec 09, 2026 |
