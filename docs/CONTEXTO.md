# CONTEXT — Cinto Alerta

> **Purpose:** single source of truth for the current state of the project. Consult it before writing code or documentation.
> **Primary source:** LaTeX proposal, September 2026 version.
> **Last review of this file:** 09/09/2026

---

## 1. Project identity

| | |
|---|---|
| **Official name** | Cinto Alerta: Fall Detector and Notifier for Older Adults |
| **Course** | Oficinas de Integração 2 — Computer Engineering, UTFPR |
| **Team** | Rafael de Andrade Fernandes · Arthur Gabriel Pellegrini Heberle · Vinícius Romualdo Silva |
| **Work distribution** | Everyone participates in all areas. There is no task owner |
| **Final delivery** | 09/12/2026 (presentation to the evaluation committee) |

> Old names that appear in previous documents and **must no longer be used:** *Projeto A.Q.U.I.*, *Cinto Guardião*, *SENTRI*.

---

## 2. Confirmed architecture

```
2x BELT (Slave)               1x MASTER                    NOTIFICATION
ESP32-S3 Supermini            ESP32-S3 Dev Module          Telegram Bot API
MPU-6050                      plugged into mains power
LiPo 500 mAh          ESP-NOW          Wi-Fi
vibration motor      ──────────►      ──────────►          caregiver's phone
button(s)             (MAC)            (HTTP/REST)
```

**Points that must not be contradicted:**

- Belt ↔ Master uses **ESP-NOW**. There is no IP between them. The identifier for each belt is its **MAC address**.
- **The master is an ESP32-S3**, not a Raspberry Pi. This is confirmed in item 02 of the component list. The Raspberry Pi 4 that appears in old documents has been discarded.
- The master hosts a **local HTTP server** and persists configuration as **JSON in LittleFS/SPIFFS**.
- Alerts are **queued** if the internet goes down and resent when the connection returns.
- **Telegram**, not WhatsApp.
- **There are two belts**, not one. The master must manage multiple slaves.

---

## 3. Confirmed hardware

| Item | Model | Qty | Note |
|---|---|---|---|
| Belt MCU | ESP32-S3 Supermini | 2 | 22.5 × 18 mm. **Has onboard charging circuit and RGB LED (GPIO48)** |
| Master MCU | ESP32-S3 Dev Module | 1 | Mains-powered |
| Inertial sensor | MPU-6050 (mini module) | 2 | 14.8 × 11.6 mm |
| Battery | LiPo Rontek 802035, 3.7 V 500 mAh | 2 | 8 × 20 × 35 mm |
| Haptic actuator | 3 V coin motor | 2 | Via NPN transistor + flyback diode |
| Transistor | BC337 or 2N2222 | — | **Do not use BC547**: 100 mA maximum current is marginal for a coin motor |
| Diode | 1N4148 | — | Flyback, required |
| Belt enclosure | 3D-printed PETG | 2 | ~87 × 51 × 14 mm, with side belt loops |

### Required MPU-6050 configuration

| Register | Value | Reason |
|---|---|---|
| `ACCEL_CONFIG` (0x1C) | `0x18` → **±16 g** | Factory default ±2 g **saturates** on impact |
| `GYRO_CONFIG` (0x1B) | `0x18` → **±2000 °/s** | Fall rotation exceeds 250 °/s |
| `SMPLRT_DIV` (0x19) | `9` → **100 Hz** | With DLPF enabled (`CONFIG = 0x03`) |

### Hardware warnings

- ⚠️ **Do not bridge the Supermini's `BOOST` jumper.** It raises charging current from 100 mA to 500 mA; the manufacturer recommends it only for batteries **larger** than 500 mAh. Ours is exactly 500 mAh.
- ⚠️ **TP4056 is unnecessary.** The Supermini already has integrated charging and `BAT+`/`BAT-` pads.
- ⚠️ ESP-NOW and Wi-Fi share the radio. On the master, the channel must be fixed consistently. Known source of intermittent bugs.
- ⚠️ The MPU-6050 **does not have** a programmable FIFO watermark interrupt (overflow only). Wake-up must be timer-based.
- ⚠️ Development boards consume milliamps even while sleeping (USB-to-serial chip). Battery-life measurement requires a bare module.

---

## 4. Current schedule

Five milestones. No hour estimates and no task owner.

| Milestone | Theme | Duration | Deadline |
|---|---|---|---|
| **M1** | Web Infrastructure and Communication | 4 weeks | **07/10/2026** |
| **M2** | Electronics, Power, and Machine Learning | 5 weeks | **11/11/2026** |
| **M3** | Prototyping and Final Testing | 2 weeks | **25/11/2026** |
| **M4** | Final Delivery (report, video, blog) | 1 week | **02/12/2026** |
| **M5** | Presentation to the evaluation committee | 1–2 weeks | **09/12/2026** |

### Milestone 1 tasks (in progress)

| ID | Task | Depends on |
|---|---|---|
| M1.1 | Web Interface | — |
| M1.1.1 | API with Telegram | M1.1 |
| M1.2 | Configuration persistence (`.json`) | — |
| M1.2.1 | Slave | M1.2 |
| M1.2.2 | Master | M1.2 |
| M1.3 | MPU-6050 tests | — |
| M1.3.1 | I²C reading | M1.3 |
| M1.3.2 | Interrupts | M1.3.1 |
| M1.3.3 | Internal memory | M1.3.1 |
| M1.4 | Belt–Master pairing system and ACK | M1.2.2 |

**Current work: M1.1 — Web Interface, front end only.** See `docs/especificacoes/frontend-configuracao.md`.

---

## 5. Closed decisions

Do not reopen without a new reason.

- **Sensor on the waist**, not the wrist. The wrist moves independently of the body and is the worst position for fall detection.
- **Telegram** instead of WhatsApp. The official WhatsApp API requires business verification and template approval, with an uncertain timeline.
- **Master on ESP32-S3.** Raspberry Pi discarded.
- **Buzzer removed.** The alert uses a vibration motor + LED.
- **Enclosure with side belt loops**, not a clip. A clip bends and breaks, and a detector that comes loose during a fall stops detecting the fall.
- **SisFall dataset** for model training (M2.4.2).
- **Everyone does everything.** No specialization by area.

---

## 6. ⚠️ Open conflicts in the proposal

These points are **inconsistent or technically problematic** in the current document. They require a decision before becoming code.

### 6.1 🔴 Deep Sleep makes the pre-impact window impossible

**What the proposal says:** RNF02 and the state flow specify that the ESP32-S3 remains in **Deep Sleep** and wakes through an interrupt from the MPU-6050 INT pin **at impact**, then collecting 2 seconds of data.

**The problem:** with this design, the system only sees what happens **after** impact. Three consequences:

1. Deep Sleep **clears RAM** — there is no circular buffer, and when the ESP wakes it restarts from `setup()` with no history.
2. Waking from Deep Sleep takes hundreds of milliseconds; the impact peak lasts 50–200 ms and is already over.
3. **Slow falls** (sliding down a wall) do not produce a peak, so they do not generate an interrupt, so the device sleeps while the person falls. And this is the most common case among frail older adults.

**Decisive aggravating factor, given the use of SisFall:** SisFall contains **continuous** recordings, including the free-fall phase before impact. If the model is trained on windows that include pre-impact data while the deployed device only supplies post-impact data, the inference input has a distribution **different from the training distribution**. The model would work on the notebook and fail on the belt.

**Proposed correction:** use **Light Sleep** with timer wake-up, with the MPU-6050 accumulating samples in its internal FIFO (1024 bytes ≈ 850 ms at 100 Hz with 6 axes). Light Sleep preserves RAM, so the buffer from the previous second survives.

**Status:** `[UNRESOLVED]` — the proposal still says Deep Sleep.

### 6.2 RF01 contradicts itself

> *"The belt must collect **continuous** data [...] **when awakened by an impact**."*

The two are mutually exclusive. Either sampling is continuous or it is event-triggered. Suggestion: split this into an FR for continuous data collection and an FR for threshold-based candidate-event detection.

**Status:** `[UNRESOLVED]`

### 6.3 RF06 requires an audible alert without a buzzer

> *"The belt must emit a specific **audible**/haptic pattern [...] when the LiPo battery is low."*

The buzzer was removed from the component list. Without it, an audible alert is impossible. RF06 needs to become haptic and visual only.

**Status:** `[UNRESOLVED]`

### 6.4 Schedule introduction text does not match the table

The paragraph before the table states that it details *"deadlines, owners, and expected deliverables"* and that *"all time estimates already include a 30% margin"*. The current table has none of those columns and no time estimates at all. The paragraph is now orphaned.

**Status:** `[UNRESOLVED]`

### 6.5 Incorrect bibliographic reference

`\bibitem{worldhealth2021}` cites *"WHO Stepwise Approach to Surveillance of Noncommunicable Disease Risk Factors"* — which is the **STEPS** protocol, about surveillance of chronic-disease risk factors. **It is not about falls.**

Correct title: *"Step Safely: Strategies for Preventing and Managing Falls Across the Life-Course"*, WHO, 2021.

**Status:** `[UNRESOLVED]`

---

## 7. Gaps: things decided in practice but absent from the proposal

They were agreed on in discussion and **do not appear** in the requirements. If they remain outside the document, they remain outside the evaluation.

### 7.1 🔴 Heartbeat / non-operational unit detection

**There is no requirement for this.** A belt with a dead battery, one that is frozen, or one that is out of range is **indistinguishable from an older adult who is fine** — in both cases the system stays silent. A safety system whose failure mode is "say nothing" is not a safety system.

Required: the belt sends a periodic signal with its MAC address and battery level; the master monitors a timeout and notifies with a **message distinct** from the fall alert.

### 7.2 Manual emergency button

Agreed: a **3 s long press** sends a request for help, covering events the accelerometer will never detect (feeling unwell, chest pain, confusion). Chosen instead of a double-click for motor accessibility.

Consequence: there are **two buttons per belt**, but item 08 of the component list provides only 2 units total.

### 7.3 🔴 Cancellation safety rule

Agreed and absent from the document: **cancellation is accepted only if the button is released in less than 2 s.**

Reason: in a face-down fall, the body can press the button against the floor. Continuous pressure would cancel a **real** alert — the worst failure mode of the entire system. A fallen body never releases; a finger does.

Additional rule: ignore any button press during the first 500 ms after the detected impact.

### 7.4 Validation methodology and metrics

The proposal does not define **how to prove that the system works**. Without a declared metric, the deliverable becomes "we assembled it and it worked in the demonstration."

Agreed metrics: sensitivity ≥ 90%, specificity ≥ 95%, false positives per day ≤ 2, notification latency < 60 s, measured battery life. Protocol: subject-wise validation (*leave-one-subject-out*).

### 7.5 Components missing from the list

- Indicator LED (or explicit use of the onboard RGB LED)
- Power switch
- Voltage-divider resistors for battery measurement — **RF06 requires measuring the battery and there is no hardware for this in the list**
- Metal heat-set inserts for the enclosure screws
- Second button per belt

### 7.6 Limitations section

Missing. Stating that SisFall falls were performed by volunteers in a controlled environment, that there is no clinical validation, and that the system depends on electrical power and internet connectivity is better than omitting these limitations.

---

## 8. Open decision: dataset

The proposal defines **SisFall** as the training source (M2.4.2), with no task for collecting the project's own dataset. This has real advantages: it eliminates the ethics committee issue, removes injury risk from data collection, and saves weeks.

But it creates a question that needs an answer:

- SisFall uses **different hardware** (other accelerometers, 200 Hz sampling rate). Transferring to the MPU-6050 at 100 Hz requires resampling and handling sensor differences.
- Without **any** data from the project's own hardware, it is not possible to report the actual performance of the built device — only the model's performance on someone else's dataset.

**Suggestion:** keep SisFall as the training basis and collect a small validation set using the project's own hardware (a few dozen simulated falls onto a mattress, plus a few hours of everyday activities), using only team members. This makes it possible to report metrics for the real device without running a full data-collection campaign.

**Status:** `[DECISION PENDING]`

---

## 9. Technical pending items

- [ ] Language for code identifiers and JSON keys
- [ ] Telegram contact: global `chat_id` or one per belt?
- [ ] Alert-message character limit (suggestion: 300)
- [ ] Pairing-window duration (suggestion: 60 s)
- [ ] Does pairing need protection against third-party belts?
- [ ] Belt and master pinout
- [ ] Actual position of the RGB LED, BOOT button, and RESET on the Supermini
- [ ] Cancellation window: proposal still uses 10 s

---

## 10. Team contacts

| Name | Institutional email | Phone |
|---|---|---|
| Rafael de Andrade Fernandes | rafael.2024@alunos.utfpr.edu.br | +55 41 99630-5003 |
| Arthur Gabriel Pellegrini Heberle | `[PLACEHODER in the proposal]` | `[PLACEHOLDER in the proposal]` |
| Vinícius Romualdo Silva | `[PLACEHOLDER in the proposal]` | +55 41 99939-5328 |

> ⚠️ Arthur's and Vinícius's email addresses, and Arthur's phone number, are still placeholders in the proposal. Real data available in the résumés: Arthur — `a.gp.heberle@gmail.com`, +55 (49) 99194-2504. Vinícius — `viniciusromualdo@gmail.com`.

---

## 11. Other documents

| Document | Content |
|---|---|
| `README.md` | Repository overview and how to run it |
| `CLAUDE.md` | Rules for AI agents |
| `docs/especificacoes/frontend-configuracao.md` | Configuration front-end specification |
| LaTeX proposal | Formal document submitted for the course |

**Obsolete documents, do not consult:** any file named *A.Q.U.I.*, *Cinto Guardião*, or any file that mentions Raspberry Pi 4 as the master, an intermediate gateway, or a buzzer.
