# CLAUDE.md

Context and rules for AI agents working in this repository.

> **Read first:** [`docs/CONTEXTO.md`](docs/CONTEXTO.md). It is the single source of truth about the current state of the project, including open conflicts. This file covers **how to work**; the context covers **what is true**.

---

## What this project is

**Cinto Alerta** — a wearable fall-detection system for older adults. Two belts with ESP32-S3 and MPU-6050 classify falls using an embedded model, alert the user through vibration and LED, and — if the user does not cancel — send an alert via ESP-NOW to a master station, which notifies a caregiver through Telegram.

Academic project for Oficinas de Integração 2, UTFPR. Final delivery on 09/12/2026. Team of three 6th-semester students, all working across all areas.

---

## Working rules

### Never invent hardware specifications
Pinout, dimensions, register addresses, current consumption: if it is not documented in this repository, **ask**. Unconfirmed values appear as `// TODO(medir):` in code and `[CONFIRMAR]` in documentation. Do not fill in a "typical" value without marking it.

### Do not add dependencies without asking
No npm, Arduino library, or Python package. Every dependency is a project decision.

### Scope is sacred
Fixed deadline, small team. Do not implement what was not requested. If you notice something missing, **point it out; do not build it**.

### Point out contradictions
If an instruction contradicts `docs/CONTEXTO.md`, or if a number does not add up, **say so before implementing**. Section 6 of the context lists known conflicts in the proposal — do not silently implement them as written.

### Language
- Interface: **English**
- Documentation and comments: **Portuguese**
- Code identifiers and JSON keys: `[CONFIRMAR — pendência aberta]`

---

## Technical facts that must not be contradicted

Summary. Details and rationale are in `docs/CONTEXTO.md`.

**Communication**
- Belt ↔ Master uses **ESP-NOW**. **There is no IP between them.** The identifier is the **MAC address** (`AA:BB:CC:DD:EE:FF`).
- ESP-NOW and Wi-Fi share the radio; on the master, the channel must be fixed consistently.
- The master is an **ESP32-S3**, not a Raspberry Pi.
- There are **two belts**. The master manages multiple slaves.

**Sensor**
- MPU-6050 at **±16 g**, **±2000 °/s**, **100 Hz**. The factory default (±2 g) saturates on impact.
- No programmable FIFO watermark interrupt — overflow only. Wake by timer.
- 1024-byte FIFO ≈ 850 ms at 100 Hz with 6 axes.

**Battery and charging**
- LiPo Rontek 802035, 500 mAh, 8 × 20 × 35 mm.
- The Supermini already has integrated charging. **Do not use TP4056.**
- **Do not bridge the `BOOST` jumper** (only for batteries above 500 mAh).

**Alert**
- **No buzzer.** Vibration + LED only.
- Transistor: BC337 or 2N2222. **Not BC547** — 100 mA is marginal for a coin motor.
- 1N4148 flyback diode required.

**Cancellation safety (critical)**
- Cancellation **is accepted only if the button is released in less than 2 s**. A fallen body on top of the button creates continuous pressure and never releases it. Without this rule, a face-down fall could cancel a real alert — the system's worst failure mode.
- Ignore button presses during the first 500 ms after the detected impact.

**Notification**
- **Telegram**, not WhatsApp.
- Alerts are queued in non-volatile memory if the internet goes down; resend on reconnection.

---

## Current work: M1.1 — Web Interface (front end only)

Full specification in [`docs/master/frontend-configuracao.md`](docs/master/frontend-configuracao.md).

**No backend. No firmware.** The interface must work when opened in a browser, using fake data, allowing fields to be filled in and the JSON to be generated.

### Constraints that must not be violated

The front end is served by the master from LittleFS. During initial configuration, the user is connected to the master's SoftAP, **with no internet access**.

1. **No external resources.** No CDN, remote font, or remote image. They will not load.
2. **No framework and no build step.** Plain HTML, CSS, and JS. No React, Vue, Tailwind, jQuery, Bootstrap.
3. **Under 100 KB** for all files combined.
4. **Mobile-first.** The caregiver configures it on a phone.
5. **`localStorage` does not store configuration.** Configuration lives in the master's LittleFS, via the API. `localStorage` is only for trivial interface preferences.

### Mandatory mock mode

A `MOCK` flag in `app.js` switches between `mock.js` (fake data, simulated pairing) and the real API. Without this, the front end could only be tested after the firmware existed, forcing development into a serial workflow.

---

## Commands

```bash
# Front end in development
cd frontend && python3 -m http.server 8000

# Firmware
cd firmware-master && pio run
cd firmware-master && pio run -t upload
cd firmware-master && pio run -t uploadfs    # uploads LittleFS

# ML analysis
cd analise-ml && python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

---

## What has already been decided and must not be reopened

- Sensor on the **waist**, not the wrist.
- **Telegram** instead of WhatsApp.
- Master on **ESP32-S3**; Raspberry Pi discarded.
- **Buzzer removed.**
- Enclosure with **side belt loops**, not a clip.
- **SisFall dataset** as the training basis.
- **Everyone does everything** — no specialization by area.
