# Specification — Master Configuration Front-end

> **Scope of this stage: FRONT-END ONLY.** No backend, no firmware. The interface must work when opened in the browser, with fake data, allowing the fields to be filled in and the JSON to be generated.
>
> The pairing state machine is implemented in C++ in the firmware. This specification defines only **what the interface displays** and **which calls it makes** — not the ESP-NOW protocol behind it.

---

## 1. Objective

A web interface served by the master where the caregiver:

1. Sees the belts that **requested pairing** and have not yet been configured
2. Associates each belt (identified by its **MAC**) with an **elderly person's name**, an **alert message**, and a **Telegram recipient**
3. Saves everything as **JSON** in the master's non-volatile memory

---

## 2. Technical constraints

| # | Constraint | Reason |
|---|---|---|
| 1 | **No external resources** — no CDN, remote font, or remote image | During initial configuration, the user is connected to the master's SoftAP, **without internet access**. External resources will not load |
| 2 | **No framework and no build step** — plain HTML, CSS, and JS | Served directly from LittleFS by a microcontroller |
| 3 | **Less than 100 KB** across all files | LittleFS space is limited |
| 4 | **Mobile-first** | The caregiver configures it from a phone |
| 5 | **`localStorage` does not store configuration** | The configuration lives on the master. `localStorage` is only for trivial interface preferences |
| 6 | Icons as **inline SVG** or Unicode characters | No icon library |

### Files

```
frontend/
├── index.html
├── style.css
├── app.js
└── mock.js      # not included in the firmware
```

---

## 3. Pairing model

> ⚠️ **Change from the previous version of this spec.** There is no "pairing mode" started by the interface, nor any countdown.

**The belt initiates the process.** When the pairing button is pressed, the belt transmits its MAC via ESP-NOW. The master **listens passively at all times** and stores any unknown MAC that appears in a pending list.

Consequence for the interface: the pending list may contain a belt that requested pairing two minutes ago or two hours ago. The caregiver does not need to have the screen open when the button is pressed.

**Confirmation happens when saving.** When the caregiver fills in the data and clicks Save, the master sends a confirmation message to the relevant MAC. The interface displays the result, but the belt's behavior when it receives that confirmation (blinking the LED, vibrating) is implemented in the firmware.

---

## 4. Data model

### Key: MAC, not IP

ESP-NOW operates at the data link layer. **There is no IP address between the belt and the master.** Each belt is identified by the board's fixed 6-byte MAC address.

Canonical format: `AA:BB:CC:DD:EE:FF` — uppercase, colon-separated.
Validation: `/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/`

### Configuration JSON

```json
{
  "versao": 1,
  "atualizado_em": "2026-09-09T14:32:11",
  "dispositivos": [
    {
      "mac": "A0:B7:65:2C:1D:E4",
      "nome_idoso": "Maria Aparecida",
      "apelido": "Grandma Maria's belt",
      "telegram_chat_id": "987654321",
      "mensagem": "ALERT: {nome} may have fallen. Detected at {hora} on {data}.",
      "ativo": true,
      "pareado_em": "2026-09-09T14:20:03"
    }
  ]
}
```

### Fields

| Field | Type | Required | Rules |
|---|---|---|---|
| `mac` | string | yes | Canonical format. **Read-only** — comes from pairing, never typed manually |
| `nome_idoso` | string | yes | 2 to 40 characters |
| `apelido` | string | no | Up to 30 characters. If empty, the interface displays the `mac` |
| `telegram_chat_id` | string | yes | See section 6. Validation: `/^-?\d{6,15}$/` |
| `mensagem` | string | yes | 10 to 300 characters |
| `ativo` | boolean | yes | Default `true`. If `false`, the master ignores alerts from this belt |
| `pareado_em` | ISO 8601 string | yes | Generated during pairing. Read-only |

### Message placeholders

| Placeholder | Replaced by |
|---|---|
| `{nome}` | value of `nome_idoso` |
| `{apelido}` | value of `apelido`, or the MAC if empty |
| `{hora}` | event time, `HH:MM` |
| `{data}` | event date, `DD/MM/YYYY` |
| `{bateria}` | belt battery level, in % |

**Mandatory live preview:** the interface displays the message with placeholders already replaced by example values, updating as the user types. Without this, someone may type `{nome]` and only discover the mistake during a real emergency.

An unknown placeholder generates a visible warning, not a silent error.

---

## 5. Screens

### 5.1 Home screen — device list

Two sections on the same screen, in this order:

**A. Awaiting configuration** (shown only if there are pending devices)

Clear visual emphasis. Each item shows:
- MAC in a monospaced font
- When the request was received (`3 minutes ago`, `2 hours ago`)
- **Configure** button → opens the form
- **Discard** button → removes it from the pending list, with confirmation

**B. Configured belts**

Each item shows:
- Elderly person's name, highlighted
- Nickname, or the MAC if there is no nickname
- MAC in a smaller monospaced font
- `active` / `inactive` indicator
- **Edit** and **Remove** buttons (removal requires confirmation)

**Periodic refresh:** while this screen is open, the interface polls the pending list every 3 seconds without reloading the page. A belt whose button is pressed now appears automatically on the screen within a few seconds.

States to handle:
- **No configured belts and no pending devices:** instruction explaining that all the user needs to do is press the pairing button on the belt
- **Loading:** indicator on the first load
- **Communication error:** clear message with an option to try again
- **Pending MAC that is already configured:** must not appear in section A

### 5.2 Configuration form

Fields, in this order:

1. **MAC** — read-only, monospaced font
2. **Elderly person's name** — text, required
3. **Belt nickname** — text, optional
4. **Telegram recipient** — see section 6
5. **Alert message** — text area, required, with:
   - character counter
   - clickable list of placeholders, which inserts them at the cursor
   - live preview
6. **Active** — toggle switch
7. **Save** and **Cancel** buttons

Behavior:
- Validation **before** sending, with a message per field
- Save button disabled while any field is invalid
- When saving, display a progress state while the master confirms with the belt
- If there are unsaved changes, warn before leaving

---

## 6. Telegram recipient field

### Why it is not a phone number

Telegram **does not allow** a bot to send messages to an arbitrary phone number. For privacy and anti-spam reasons, the bot can only message someone who has **already started a conversation with it**. The identifier used is not the phone number, but the **`chat_id`** — an internal number generated by Telegram.

This changes the field label: it is not "caregiver's number", but "Telegram recipient".

### Expandable instructions

Next to the field, a **"How do I get this number?"** link expands a block with the steps. Collapsed by default. Content:

1. Open Telegram on the phone that will receive the alerts
2. Find the project's bot and send any message to it
3. Search for `@userinfobot` and send `/start`
4. It replies with your `Id` — that is the number that goes in the field

> Step 2 is essential and is the most commonly forgotten: without it, the bot does not have permission to message that person, and sending fails even with the correct `chat_id`.

### Test button

Next to the field, an **Send test** button. It calls the master, which attempts to send a real message to the provided `chat_id`.

**The button must distinguish the reasons for failure**, because there are four situations with completely different solutions. A generic "failed" leaves the user with no way forward.

| Response | Message in the interface |
|---|---|
| `ok` | Test message sent. Check Telegram. |
| `chat_id_invalido` | Invalid format. It must be a number, with no spaces. |
| `chat_desconhecido` | Telegram rejected it. The person needs to send a message to the bot before they can receive alerts. |
| `sem_token` | The master does not yet have the Telegram bot configured. |
| `sem_internet` | The master has no internet connection. Testing is only possible after configuring Wi-Fi. |

Rules:
- Button disabled while the field is empty or invalid
- Loading state while waiting for a response
- **The test is not required in order to save.** It validates, but does not block saving — Wi-Fi may not yet be configured when the device is registered

> **Known dependency:** the test only works if the bot token is already stored on the master and the master has internet access. Configuring these two things is **outside the scope of this stage** — but the interface needs to handle their absence with the `sem_token` and `sem_internet` messages above instead of showing a generic error.

---

## 7. API contract

The front-end is written against this contract. Since the backend does not exist yet, `mock.js` implements these routes exactly.

| Method | Route | Body / Response |
|---|---|---|
| `GET` | `/api/dispositivos` | → `{ "dispositivos": [...] }` |
| `GET` | `/api/pendentes` | → `{ "pendentes": [ { "mac": "...", "recebido_em": "..." } ] }` |
| `PUT` | `/api/dispositivos/{mac}` | ← device object · → `{ "ok": true, "confirmado_pelo_cinto": true }` |
| `DELETE` | `/api/dispositivos/{mac}` | → `{ "ok": true }` |
| `DELETE` | `/api/pendentes/{mac}` | → `{ "ok": true }` |
| `POST` | `/api/teste-telegram` | ← `{ "chat_id": "..." }` · → `{ "ok": true }` or `{ "ok": false, "erro": "chat_desconhecido" }` |
| `GET` | `/api/config.json` | → complete JSON, to download as a backup |

Errors follow this format:

```json
{ "ok": false, "erro": "mac_invalido", "mensagem": "MAC address in invalid format." }
```

Regarding the `PUT`: the `confirmado_pelo_cinto` field indicates whether the master was able to confirm with the device. The configuration is saved either way; if it is `false`, the interface warns that the belt did not respond to the confirmation, without treating this as a save failure.

---

## 8. Mock mode

`app.js` begins with:

```javascript
const MOCK = true;
const API_BASE = '';
```

With `MOCK = true`:

- Data comes from arrays in `mock.js`: two configured devices and one pending device
- Calls simulate 300 ms latency so loading states are visible
- **A new pending MAC appears automatically after ~15 seconds**, simulating someone pressing the belt button and allowing the periodic refresh to be tested
- `POST /api/teste-telegram` alternates between responses to exercise all error messages
- Saving changes the in-memory array and logs the resulting JSON to the console

Without mock mode, the front-end could only be tested after the firmware existed, forcing development to proceed serially.

---

## 9. Interface and accessibility

The user of this screen is the caregiver, typically an adult child. Even so:

- Minimum touch target of **44 × 44 px**
- Body text of **16 px** or more (anything smaller causes iOS to automatically zoom in on fields)
- Minimum contrast ratio of **4.5:1**
- Every field has an associated `<label>`, not just a `placeholder`
- Visible keyboard focus
- Destructive actions always require confirmation
- Works from a width of 320 px

Errors must say what to do, not only what is wrong.

---

## 10. Outside the scope of this stage

- Master Wi-Fi configuration
- Telegram bot token registration
- Event history
- Cancellation window adjustment
- Authentication
- Any backend or firmware code
- The pairing state machine (will be implemented in C++)

---

## 11. Acceptance criteria

- [ ] Opens with `python3 -m http.server` and works without hardware or internet
- [ ] No requests to external domains (check the Network tab)
- [ ] Total file size below 100 KB
- [ ] Pending and configured devices listed in separate sections
- [ ] New pending device appears automatically, without reloading the page
- [ ] Configure a pending device and it moves to the configured list
- [ ] Discarding a pending device works, with confirmation
- [ ] `chat_id` instructions expand and collapse
- [ ] Test button displays a distinct message for each of the five results
- [ ] Message preview replaces all placeholders
- [ ] Validation prevents saving without a name, `chat_id`, or message
- [ ] Generated JSON matches the format in section 4 exactly
- [ ] Usable on a 320 px screen
- [ ] Empty, loading, and error states implemented

---

## 12. Open items

- [ ] Language of code identifiers and JSON keys
- [ ] Can one belt have more than one Telegram recipient? The current model allows one per device
- [ ] Does discarding a pending device prevent that MAC from appearing again, or does it reappear if the button is pressed again?
- [ ] Does pairing require protection against a third-party belt registering?

> **Resolved:** the master is an **ESP32-S3 Dev Module**, confirmed in the proposal's component list.
> **Resolved:** each device has **its own recipient** on Telegram, not a global one.
> **Resolved:** pairing is **initiated by the belt**; the master only listens and lists devices.
