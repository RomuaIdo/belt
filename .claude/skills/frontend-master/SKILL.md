---
name: frontend-master
description: Rules and checks for writing or modifying the Cinto Alerta master configuration front end. ALWAYS use this when the work involves files in frontend/ or firmware-master/data/, or any HTML, CSS, or JavaScript in this project. Covers environment constraints (served by a microcontroller, no internet), the API contract, and the mandatory checklist before considering the task complete.
---

# Master front end — Cinto Alerta

This front end is served by an **ESP32-S3** from **LittleFS**, and the user accesses it while connected to the **master's own SoftAP, with no internet access**.

Read `docs/especificacoes/frontend-configuracao.md` before starting. This file is the operational summary of the rules.

## Absolute prohibitions

Violating any of these will make the page break in production, even if it works on your machine.

1. **No external resources.** No CDN, no `fonts.googleapis.com`, no remote images, no `<script src="https://...">`. The device has no internet access when this page is used. External resources will not load, and the page will open incomplete.
2. **No frameworks.** No React, Vue, Svelte, Tailwind, Bootstrap, jQuery, Alpine. Plain HTML, CSS, and JavaScript only.
3. **No build step.** No npm, no bundler, no transpiler. The files you write are exactly the files the master serves.
4. **No `localStorage` for configuration.** Configuration lives in the master's LittleFS, via the API. `localStorage` is acceptable only for trivial interface preferences, such as the selected tab.
5. **100 KB budget** for `index.html`, `style.css`, and `app.js` combined, before gzip.

If a task seems to require any of these things, **stop and ask**. Do not work around it.

## Accepted substitutes

| Instead of | Use |
|---|---|
| Icon library | Inline SVG or Unicode character |
| External font | System stack: `system-ui, -apple-system, sans-serif` |
| CSS framework | Plain CSS with custom properties in `:root` |
| Reactive framework | `textContent`, `classList`, and functions that redraw the modified section |
| HTTP client | Native `fetch` |

## Structure

```
frontend/
├── index.html
├── style.css
├── app.js
└── mock.js      # DOES NOT go into firmware-master/data/
```

`app.js` starts with:

```javascript
const MOCK = true;
const API_BASE = '';
```

With `MOCK = true`, all calls are handled by `mock.js`, with a simulated latency of 300 ms. **Never remove mock mode** — it is what makes it possible to develop and demonstrate the interface before the firmware exists.

## API contract

Do not invent routes. These are the ones that exist:

| Method | Route |
|---|---|
| `GET` | `/api/dispositivos` |
| `GET` | `/api/pendentes` |
| `PUT` | `/api/dispositivos/{mac}` |
| `DELETE` | `/api/dispositivos/{mac}` |
| `DELETE` | `/api/pendentes/{mac}` |
| `POST` | `/api/teste-telegram` |
| `GET` | `/api/config.json` |

Errors are returned as `{ "ok": false, "erro": "codigo", "mensagem": "texto" }`.

## Domain rules that are often implemented incorrectly

- **The identifier is the MAC address, not an IP address.** ESP-NOW operates at the data-link layer; there is no IP between the belt and the master. Format: `AA:BB:CC:DD:EE:FF`, uppercase.
- **The MAC address is never typed in.** It comes from pairing and is read-only in the form.
- **Pairing is initiated by the belt**, not by the interface. There is no "pair" button that starts a countdown. The master listens passively; the interface polls `/api/pendentes` every 3 s and displays what arrived.
- **The Telegram field is `chat_id`, not a phone number.** A Telegram bot cannot message an arbitrary phone number — only users who have already started a conversation with it.
- **Saving does not fail if the belt does not confirm.** The `PUT` returns `confirmado_pelo_cinto`; if it is `false`, warn that confirmation was not received, but treat the save as successful.
- **The Telegram test button has five distinct outcomes** (`ok`, `chat_id_invalido`, `chat_desconhecido`, `sem_token`, `sem_internet`), each with its own message. A generic "failed" leaves the user without guidance.

## Mandatory verification before finishing

Use Chrome DevTools MCP. These items **cannot be verified by reading code**:

- [ ] Network tab: **no requests to external domains**
- [ ] Resize to **320 px** wide: nothing overflows or overlaps
- [ ] Console: **no errors and no warnings**
- [ ] Add up file sizes: **under 100 KB**
- [ ] Touch targets at least **44 × 44 px**
- [ ] Every field has an associated `<label>`, not just a `placeholder`
- [ ] Keyboard focus is visible when navigating with Tab
- [ ] Empty, loading, and error states are implemented and visible
- [ ] Message preview replaces all placeholders
- [ ] Text in form fields is 16 px or larger (smaller than that makes iOS zoom automatically)

## When changing CSS

Open the page in the browser and take a screenshot before saying you are finished. Layout cannot be verified by reading code.
