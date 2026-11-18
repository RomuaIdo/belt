<div align="center">

# Cinto Alerta

### A belt that detects elderly falls and automatically calls for help

![Status](https://img.shields.io/badge/status-in%20development-yellow)
![Institution](https://img.shields.io/badge/UTFPR-Engenharia%20de%20Computa%C3%A7%C3%A3o-blue)
![Technology](https://img.shields.io/badge/TinyML-ESP32--S3-orange)

</div>

---

## The problem is not the fall. It is the time spent on the floor.

Falls are among the leading causes of injury and loss of autonomy in elderly people. But what determines severity is not just the impact — it is **how long the person remains unattended**.

Someone who falls alone at home and cannot get up may spend hours on the floor. During this interval, injuries worsen and the risk of complications rises.

The obvious answer — "they can call someone" — fails precisely when it is needed most. If there is loss of consciousness, a hip fracture, or the phone was left in another room, no call is made.

---

## How it works

The flow below is what the firmware does today; see [Current status](#current-status) for what is still missing, and the [belt guide](slave/README.md) and [base guide](master/README.md) for details and commands.

```text
+-----------------+      +-------------------+
|      Fall       | ---> | Belt vibrates and |
|    detected     |      | turns on the LED  |
+-----------------+      +-------------------+
                                   |
                                   v
                         /-------------------\
                        <    User cancels?    >
                         \-------------------/
                              /         \
                        Yes  /           \  No
                            v             v
                     +------------+  +-------------------+
                     |   Alarm    |  |   Alert sent to   |
                     | discarded  |  |     the base      |
                     +------------+  +-------------------+
                                               |
                                               v
                                     +-------------------+
                                     | Caregiver receives|
                                     |    on Telegram    |
                                     +-------------------+
```

The belt detects falls on its own, using a motion sensor and an artificial intelligence model that runs **on the device itself** — without internet, without cloud, without sending data anywhere.

If the user is fine, a press of the button cancels the alert. If there is no response, the alert is sent to a family member or caregiver's phone.

In more detail, the belt uses a BMI270 motion sensor. A cheap rule picks fall candidates (a strong impact followed by the body ending in another orientation), and a small neural network trained on the SisFall dataset confirms them. A confirmed fall gives the user 5 seconds to cancel. The belt then resends the alert until the base confirms it received it.

---

## What the device does

- **Detects falls automatically** — without relying on the person pressing anything
- **Warns before sounding an alarm** — vibration and LED provide a few seconds to cancel
- **Manual emergency button** — for malaise, pain, or confusion, which no motion sensor can detect
- **Notifies via Telegram** — customizable message for each caregiver
- **Warns when battery is low** — before leaving the user unprotected
- **Works without relying on home Wi-Fi** — the belt communicates directly with the base
- **Alerts if the device itself stops working** — a silent device is not mistaken for "everything is fine"

### Current status

| Feature | Status |
|---|---|
| Fall detection on the belt (BMI270 + heuristic + CNN) | Implemented, being tested on hardware |
| Cancellation window (5 s, LED, buzzer, and vibration) | Implemented |
| Manual emergency button (one click) | Implemented |
| Belt to base radio (ESP-NOW), with pairing and alert acknowledgment | Implemented |
| Light sleep on the belt, woken by motion or buttons | Implemented, being tested on hardware |
| Telegram notifications with a persistent retry queue | Implemented |
| Configuration page on the base (Wi-Fi, Telegram, belts) | Implemented |
| Low battery warning | Not implemented yet |
| Heartbeat (alert when the belt goes silent) | Not implemented yet |

---

## Design decisions

<details>
<summary><b>Why on the waist, and not on the wrist?</b></summary>

<br>

This was the first technical decision of the project, and perhaps the most important one.

The original idea was a smartwatch — more modern, socially more acceptable. The problem is that **the wrist is the worst possible location for fall detection**.

The arm moves independently from the body. Hitting a hand on a table, applauding, brushing teeth, taking off the watch and putting it down — all of these produce motion signatures very similar to a fall. The result is constant false alarms, and a device that triggers false alarms every day is a device the user turns off.

The waist solves this because **it follows the body's center of mass**: it only truly moves when the entire body moves.

</details>

<details>
<summary><b>Why is there a cancellation window?</b></summary>

<br>

Detecting an impact is easy. Distinguishing **"fell down"** from **"sat down quickly on the couch"** is a challenge that the field has yet to solve reliably.

The cancellation window turns a critical false alarm into a minor inconvenience: without it, every false alarm startles the family; with it, it is just a button press.

And there is a bonus — every cancellation is a real-world example of "this was not a fall", which can be used to improve the model.

</details>

<details>
<summary><b>Why does the device warn when it stops functioning?</b></summary>

<br>

A belt with a dead battery, frozen, or out of range is **indistinguishable from an elderly person who is doing fine** — in both cases, the system remains silent.

A safety system whose failure mode is "notify nothing" is not a safety system. For this reason, the belt will send periodic heartbeat signals, and the base will notify the caregiver if they stop arriving — with a distinct message, clarifying that the issue is with the device, not the person. (Planned; not implemented yet.)

</details>

---

## System architecture

```text
+-----------------------+                    +-----------------------+
|         BELT          |                    |         BASE          |
|   Motion sensor       |    Direct radio    |   Wall-powered        |
|   Embedded AI         | -----------------> |   Configuration page  |
|   Battery             |      (ESP-NOW)     |                       |
+-----------------------+                    +-----------------------+
                                                         |
                                                         | Internet
                                                         v
+-----------------------+                    +-----------------------+
|       Caregiver       | <----------------- |       Telegram        |
|  (Smartphone alert)   |                    +-----------------------+
+-----------------------+
```

The **base** stays plugged into a wall outlet at home and hosts a configuration page accessible via smartphone, where the family registers each belt, the elderly person's name, the alert message, and who should receive it.

If the internet connection is down when an alert occurs, the base **stores the occurrence in a persistent queue** and dispatches it as soon as the connection is restored.

---

## Repository organization

| Path | Content |
|---|---|
| [master/](master/) | **Base** firmware (PlatformIO, ESP32-S3-DevKitC-1 N16R8) and its configuration page in [master/frontend/](master/frontend/) |
| [slave/](slave/) | **Belt** firmware (PlatformIO, ESP32-S3 Super Mini), fall detection prototypes, and the fall model training in [slave/prototypes/](slave/prototypes/) |
| [shared/](shared/) | Code used by both firmwares: the ESP-NOW wire protocol and MAC address helpers |
| [docs/](docs/) | Project plan |

**Base (`master/`).** An always-on Wi-Fi access point and station. It serves the configuration page from LittleFS, receives pairing requests and alerts from the belts over ESP-NOW, and sends Telegram messages on FreeRTOS tasks with retry and backoff. Alerts wait in a power-loss-safe queue on LittleFS. See the [base guide](master/README.md) to set it up, build, and test.

**Belt (`slave/`).** Pairs with the base and detects falls with a BMI270 sensor and a small CNN that runs on the chip. It sends alerts (from a fall or the button) until the base acknowledges them, shows its state on an LED, buzzer, and vibration motor, and stays in light sleep while nothing is happening. See the [belt guide](slave/README.md) for pins, behavior, build, and test.

Both firmwares have Unity test suites under their `test/` folders.

---

## What this project is not

> [!WARNING]
> **This is not a medical device.** It is an academic prototype without clinical validation or regulatory certification.

- Training data comes from simulated falls performed by healthy young adults. Real falls in elderly people are different — slower, with fewer protective reflexes, landing on hard surfaces.
- Relies on electrical power in the home, a working internet connection, and a caregiver with an accessible smartphone.
- **Does not replace human presence.** It reduces the time until help arrives; it does not eliminate the risk of falling.

---
<div align="center">

## Team

Project developed for the **Oficinas de Integração 2** course
Engenharia de Computação · Universidade Tecnológica Federal do Paraná

**Rafael de Andrade Fernandes** · **Arthur Gabriel Pellegrini Heberle** · **Vinícius Romualdo Silva**

<br>

*Curitiba, PR · 2026*

</div>
