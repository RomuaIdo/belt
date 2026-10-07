// Use ?mock in URL to test UI with mock.js.
const MOCK = new URLSearchParams(location.search).has('mock');
const API_BASE = '';

let mockApi = null;
if (MOCK) {
  ({ mockApi } = await import('./mock.js'));
}

// Domain rules

const CHAT_RE = /^-?\d{1,20}$/;
const MAC_RE = /^([0-9A-F]{2}:){5}[0-9A-F]{2}$/;
const NOME_MIN = 2, NOME_MAX = 40;
const MSG_MIN = 10, MSG_MAX = 300;

const DEFAULT_MSG = 'ALERT: {nome} may have fallen. Detected at {hora} on {data}.';

const PLACEHOLDERS = {
  '{nome}': 'Maria Aparecida',
  '{hora}': '14:32',
  '{data}': '09/09/2026',
};

// Telegram response mapping: code -> [message, status]
const TELEGRAM_MESSAGES = {
  ok: ['Message sent. Check Telegram.', 'ok'],
  chat_id_invalido: ['That chat number is not valid.', 'bad'],
  chat_desconhecido: ['Telegram rejected this chat. The person needs to open the bot and press Start first.', 'bad'],
  sem_token: ['Set the bot token in Settings first.', 'bad'],
  token_invalido: ['Telegram did not accept the token. Check it in Settings.', 'bad'],
  sem_internet: ['The master could not reach Telegram. Check its Wi-Fi in Settings.', 'bad'],
  sem_relogio: ['The master is still setting its clock. Try again in a few seconds.', 'bad'],
  erro_telegram: ['Telegram returned an unexpected error. Try again.', 'bad'],
  sem_master: ["Can't reach the master. Check that you're connected to its Wi-Fi.", 'bad'],
};

function telegramMessage(code) {
  return TELEGRAM_MESSAGES[code] || TELEGRAM_MESSAGES.erro_telegram;
}

// API layer

async function api(method, path, body) {
  if (MOCK) return mockApi(method, path, body);
  const opts = { method, headers: {} };
  if (body !== undefined) {
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(API_BASE + path, opts);
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    const err = new Error(data.mensagem || `HTTP ${res.status}`);
    err.code = data.erro;
    throw err;
  }
  return data;
}

// Utilities

function h(tag, attrs = {}, ...kids) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v);
    else node.setAttribute(k, v);
  }
  for (const kid of kids) if (kid != null) node.append(kid);
  return node;
}

const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function relativeTime(iso) {
  const then = new Date(iso).getTime();
  if (Number.isNaN(then)) return 'a moment ago';
  const s = Math.max(0, (Date.now() - then) / 1000);
  if (s < 45) return 'just now';
  const m = Math.round(s / 60);
  if (m < 60) return m <= 1 ? '1 minute ago' : `${m} minutes ago`;
  const hr = Math.round(m / 60);
  if (hr < 24) return hr === 1 ? '1 hour ago' : `${hr} hours ago`;
  const d = Math.round(hr / 24);
  return d === 1 ? '1 day ago' : `${d} days ago`;
}

function setResult(el, text, kind) {
  el.textContent = text;
  el.className = el.className.split(' ')[0] + (kind ? ' ' + kind : '');
}

let toastTimer = null;
function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}

// Confirmation dialog

function askConfirm(text, okLabel = 'Confirm') {
  return new Promise((resolve) => {
    const dlg = $('confirm');
    $('confirm-text').textContent = text;
    $('confirm-ok').textContent = okLabel;
    dlg.returnValue = 'cancel';
    dlg.showModal();
    dlg.addEventListener('close', () => resolve(dlg.returnValue === 'ok'), { once: true });
  });
}

// View switching

function showView(name) {
  for (const view of ['home', 'settings', 'form']) {
    $(`view-${view}`).hidden = view !== name;
  }
  window.scrollTo(0, 0);
}

// Home view

let knownDevices = [];
let configuredMacs = new Set();
let knownPendingMacs = new Set();

function setHomeStatus(text, { error = false } = {}) {
  $('home-status').hidden = false;
  $('home-status').classList.toggle('is-error', error);
  $('home-status-text').textContent = text;
  $('home-retry').hidden = !error;
  $('section-pending').hidden = true;
  $('section-devices').hidden = true;
}

function clearHomeStatus() {
  $('home-status').hidden = true;
  $('home-status').classList.remove('is-error');
  $('home-retry').hidden = true;
}

async function loadHome({ initial = false } = {}) {
  showView('home');
  if (initial) setHomeStatus('Loading belts…');
  try {
    const [dev, pend] = await Promise.all([
      api('GET', '/api/dispositivos'),
      api('GET', '/api/pendentes'),
    ]);
    knownDevices = dev.dispositivos || [];
    configuredMacs = new Set(knownDevices.map((d) => d.mac.toUpperCase()));
    const pendentes = (pend.pendentes || [])
      .filter((p) => !configuredMacs.has(p.mac.toUpperCase()));

    clearHomeStatus();

    if (!knownDevices.length && !pendentes.length) {
      setHomeStatus('No belts yet. Press the pairing button on a belt and it will show up here.');
      knownPendingMacs = new Set();
      return;
    }

    renderDevices(knownDevices);
    renderPending(pendentes, { animateNew: !initial });
    knownPendingMacs = new Set(pendentes.map((p) => p.mac.toUpperCase()));
  } catch (e) {
    setHomeStatus("Can't reach the master. Check that you're connected to its Wi-Fi.", { error: true });
  }
}

function renderDevices(devices) {
  const sec = $('section-devices');
  const list = $('device-list');
  list.replaceChildren();
  if (!devices.length) { sec.hidden = true; return; }
  sec.hidden = false;
  for (const d of devices) list.append(deviceRow(d));
}

function renderPending(pendentes, { animateNew = false } = {}) {
  const sec = $('section-pending');
  const list = $('pending-list');
  list.replaceChildren();
  if (!pendentes.length) { sec.hidden = true; return; }
  sec.hidden = false;
  $('pending-count').textContent = String(pendentes.length);
  for (const p of pendentes) {
    const isNew = animateNew && !knownPendingMacs.has(p.mac.toUpperCase());
    list.append(pendingRow(p, isNew));
  }
}

function pendingRow(p, isNew) {
  return h('li', { class: isNew ? 'just-arrived' : null },
    h('div', { class: 'row-main' }, h('span', { class: 'mac', text: p.mac.toUpperCase() })),
    h('p', { class: 'dev-meta', text: `Heard ${relativeTime(p.recebido_em)}` }),
    h('div', { class: 'row-actions' },
      h('button', { class: 'btn btn-primary', type: 'button', onclick: () => startSetup(p) }, 'Set up'),
      h('button', { class: 'btn btn-quiet', type: 'button', onclick: () => discardPending(p) }, 'Discard'),
    ),
  );
}

function deviceRow(d) {
  const count = (d.chat_ids || []).length;
  return h('li', {},
    h('div', { class: 'row-main' },
      h('span', { class: 'dev-name', text: d.nome }),
      h('span', { class: 'dev-alias', text: count === 1 ? '1 recipient' : `${count} recipients` }),
    ),
    h('p', { class: 'mac mac-sm', text: d.mac.toUpperCase() }),
    h('div', { class: 'row-actions' },
      h('button', { class: 'btn', type: 'button', onclick: () => startEdit(d) }, 'Edit'),
      h('button', { class: 'btn btn-quiet', type: 'button', onclick: () => removeDevice(d) }, 'Remove'),
    ),
  );
}

async function discardPending(p) {
  const ok = await askConfirm(
    'Discard this pairing request? The belt can ask again by pressing its button.', 'Discard');
  if (!ok) return;
  try {
    await api('DELETE', `/api/pendentes/${encodeURIComponent(p.mac)}`);
    toast('Pairing request discarded.');
    loadHome();
  } catch (e) {
    toast("Couldn't discard it. Try again.");
  }
}

async function removeDevice(d) {
  const ok = await askConfirm(`Remove ${d.nome}'s belt? Alerts from it will stop.`, 'Remove');
  if (!ok) return;
  try {
    await api('DELETE', `/api/dispositivos/${encodeURIComponent(d.mac)}`);
    toast(`${d.nome}'s belt was removed.`);
    loadHome();
  } catch (e) {
    toast("Couldn't remove it. Try again.");
  }
}

// Pending devices polling

async function refreshPending() {
  try {
    const pend = await api('GET', '/api/pendentes');
    const pendentes = (pend.pendentes || [])
      .filter((p) => !configuredMacs.has(p.mac.toUpperCase()));

    // Reload full view if empty/error state now has items
    if (!$('home-status').hidden) {
      if (pendentes.length) loadHome();
      return;
    }
    renderPending(pendentes, { animateNew: true });
    knownPendingMacs = new Set(pendentes.map((p) => p.mac.toUpperCase()));
  } catch (e) {
    // Silently ignore polling errors
  }
}

// Settings

async function openSettings() {
  showView('settings');
  await refreshStatus();
}

async function refreshStatus() {
  try {
    const status = await api('GET', '/api/status');
    applyStatus(status);
    return status;
  } catch (e) {
    return null;
  }
}

function applyStatus(status) {
  const { wifi, telegram } = status;

  $('wifi-badge').textContent = wifi.conectado ? 'Connected' : 'Not connected';
  $('wifi-badge').classList.toggle('is-off', !wifi.conectado);
  $('wifi-current').textContent = !wifi.ssid
    ? 'No network saved yet.'
    : wifi.conectado
      ? `Connected to "${wifi.ssid}". On that network, open http://${wifi.endereco} (or http://${wifi.ip}). The master reconnects by itself when it powers on.`
      : `Saved network: "${wifi.ssid}". The master is not connected to it right now.`;
  if (wifi.ssid && !$('wifi-ssid').value) $('wifi-ssid').value = wifi.ssid;

  $('tg-badge').textContent = telegram.configurado ? 'Token saved' : 'Not set up';
  $('tg-badge').classList.toggle('is-off', !telegram.configurado);
  $('tg-token').placeholder = telegram.configurado ? 'Token saved. Paste a new one to replace it.' : '';
}

// Wi-Fi

function signalLabel(rssi) {
  if (rssi >= -60) return 'strong';
  if (rssi >= -75) return 'good';
  return 'weak';
}

async function scanNetworks() {
  const btn = $('wifi-scan');
  const result = $('wifi-scan-result');
  btn.disabled = true;
  setResult(result, 'Searching… this takes a few seconds.');
  try {
    const res = await api('GET', '/api/wifi/redes');
    const list = $('wifi-list');
    list.replaceChildren();
    for (const net of res.redes || []) {
      const button = h('button', { type: 'button', class: 'net-btn', onclick: () => pickNetwork(net, button) },
        h('span', { text: net.ssid }),
        h('span', { class: 'net-meta', text: `${net.aberta ? 'open' : 'secured'} · ${signalLabel(net.rssi)}` }),
      );
      list.append(h('li', {}, button));
    }
    list.hidden = !list.children.length;
    setResult(result, list.children.length ? 'Tap your network.' : 'No networks found. Try again.');
  } catch (e) {
    setResult(result, "Couldn't search for networks. Try again.", 'bad');
  } finally {
    btn.disabled = false;
  }
}

function pickNetwork(net, button) {
  for (const b of document.querySelectorAll('.net-btn')) b.classList.remove('is-selected');
  button.classList.add('is-selected');
  $('wifi-ssid').value = net.ssid;
  $('wifi-pass').value = '';
  $('wifi-pass').focus();
}

async function connectWifi() {
  const ssid = $('wifi-ssid').value;
  const pass = $('wifi-pass').value;
  const result = $('wifi-result');
  if (!ssid) { setResult(result, 'Enter the network name.', 'bad'); return; }
  if (pass && (pass.length < 8 || pass.length > 63)) {
    setResult(result, 'The password must have 8 to 63 characters.', 'bad');
    return;
  }

  const btn = $('wifi-connect');
  btn.disabled = true;
  setResult(result, 'Connecting…');
  try {
    await api('POST', '/api/wifi', { ssid, senha: pass });

    // Wait for connection; network drops during channel switches are ignored.
    await sleep(2500);
    const deadline = Date.now() + 25000;
    while (Date.now() < deadline) {
      try {
        const status = await api('GET', '/api/status');
        applyStatus(status);
        if (status.wifi.conectado) {
          setResult(result,
            `Connected. On "${status.wifi.ssid}", open http://${status.wifi.endereco} (or http://${status.wifi.ip}).`,
            'ok');
          return;
        }
      } catch (e) { // Retry
      }
      await sleep(1500);
    }
    setResult(result,
      "Couldn't connect. Check the password and that this is a 2.4 GHz network. The master keeps trying.",
      'bad');
  } catch (e) {
    setResult(result, e.message, 'bad');
  } finally {
    btn.disabled = false;
  }
}

// Telegram token

function onTokenInput() {
  const hasText = $('tg-token').value.trim().length > 0;
  $('tg-test').disabled = !hasText;
  $('tg-save').disabled = true;  // Enabled only after successful test
  setResult($('tg-result'), '');
}

async function testToken() {
  const btn = $('tg-test');
  const result = $('tg-result');
  btn.disabled = true;
  setResult(result, 'Testing…');
  try {
    const res = await api('POST', '/api/telegram/testar-token', { token: $('tg-token').value.trim() });
    if (res.ok) {
      setResult(result, `Bot ${res.bot} works. Press Save.`, 'ok');
      $('tg-save').disabled = false;
    } else {
      setResult(result, telegramMessage(res.erro)[0], 'bad');
    }
  } catch (e) {
    setResult(result, telegramMessage('sem_master')[0], 'bad');
  } finally {
    btn.disabled = false;
  }
}

async function saveToken() {
  const btn = $('tg-save');
  btn.disabled = true;
  try {
    await api('PUT', '/api/telegram/token', { token: $('tg-token').value.trim() });
    $('tg-token').value = '';
    $('tg-test').disabled = true;
    setResult($('tg-result'), 'Token saved.', 'ok');
    toast('Token saved.');
    refreshStatus();
  } catch (e) {
    btn.disabled = false;
    setResult($('tg-result'), `Couldn't save: ${e.message}`, 'bad');
  }
}

// Form view

const form = $('device-form');
let editing = null;      // Device being edited
let formDirty = false;
const touched = new Set();

let chatOptions = new Map();   // chat_id -> display name
const chatChecked = new Set(); // Selected chat_ids

const fMac = $('f-mac');
const fNome = $('f-nome');
const fMsg = $('f-msg');
const saveBtn = $('f-save');
const testBtn = $('f-test');
const findBtn = $('f-find');

function startSetup(p) {
  editing = { mac: p.mac.toUpperCase(), nome: '', mensagem: DEFAULT_MSG, chat_ids: [] };
  $('form-title').textContent = 'Set up belt';
  openForm();
}

function startEdit(d) {
  editing = { ...d, mac: d.mac.toUpperCase(), chat_ids: [...(d.chat_ids || [])] };
  $('form-title').textContent = 'Edit belt';
  openForm();
}

function openForm() {
  fMac.textContent = editing.mac;
  fNome.value = editing.nome || '';
  fMsg.value = editing.mensagem || '';

  // Pre-check saved recipients even if omitted from getUpdates
  chatOptions = new Map(editing.chat_ids.map((id) => [id, 'Saved recipient']));
  chatChecked.clear();
  editing.chat_ids.forEach((id) => chatChecked.add(id));
  renderChats();

  touched.clear();
  formDirty = false;
  $('form-warning').hidden = true;
  $('f-save-state').hidden = true;
  setResult($('f-find-result'), '');
  setResult($('f-test-result'), '');
  collapseHelp();

  updateCounter();
  updatePreview();
  validate();
  showView('form');
}

function collapseHelp() {
  $('f-chat-help').hidden = true;
  $('f-chat-help-toggle').setAttribute('aria-expanded', 'false');
}

// Telegram recipients

function renderChats() {
  const list = $('f-chats');
  list.replaceChildren();
  for (const [id, name] of chatOptions) {
    const box = h('input', { type: 'checkbox', value: id });
    box.checked = chatChecked.has(id);
    box.addEventListener('change', () => {
      if (box.checked) chatChecked.add(id); else chatChecked.delete(id);
      formDirty = true;
      touched.add('chat_ids');
      validate();
    });
    list.append(h('li', {},
      h('label', { class: 'chat-opt' },
        box,
        h('span', { class: 'chat-name', text: name }),
        h('span', { class: 'mac mac-sm', text: id }),
      ),
      h('span', { class: 'chat-result', 'data-chat': id }),
    ));
  }
}

function chatResultEl(id) {
  return $('f-chats').querySelector(`[data-chat="${CSS.escape(id)}"]`);
}

async function findChats() {
  const result = $('f-find-result');
  findBtn.disabled = true;
  setResult(result, 'Searching…');
  try {
    const res = await api('GET', '/api/telegram/conversas');
    if (!res.ok) {
      const [text] = telegramMessage(res.erro);
      setResult(result, text, 'bad');
      return;
    }
    for (const chat of res.conversas || []) chatOptions.set(chat.chat_id, chat.nome);
    renderChats();
    setResult(result, (res.conversas || []).length
      ? 'Tick who should receive this belt\'s alerts.'
      : 'Nobody has written to the bot yet. See "How do people show up here?".');
  } catch (e) {
    setResult(result, telegramMessage('sem_master')[0], 'bad');
  } finally {
    findBtn.disabled = false;
  }
}

async function sendTest() {
  const ids = [...chatChecked];
  if (!ids.length) return;
  testBtn.disabled = true;
  setResult($('f-test-result'), 'Sending…');
  for (const id of ids) {
    const el = chatResultEl(id);
    if (el) { el.textContent = ''; el.className = 'chat-result'; }
  }

  let sent = 0;
  for (const id of ids) {
    let code;
    try {
      const res = await api('POST', '/api/teste-telegram', { chat_id: id });
      code = res.ok ? 'ok' : (res.erro || 'erro_telegram');
    } catch (e) {
      code = 'sem_master';
    }
    const [text, kind] = telegramMessage(code);
    if (code === 'ok') sent += 1;
    const el = chatResultEl(id);
    if (el) { el.textContent = text; el.className = 'chat-result ' + kind; }
  }
  setResult($('f-test-result'), `Sent to ${sent} of ${ids.length}.`, sent === ids.length ? 'ok' : 'bad');
  validate();
}

// Validation

function fieldErrors() {
  const errs = {};
  const nome = fNome.value.trim();
  if (nome.length < NOME_MIN || nome.length > NOME_MAX) {
    errs.nome = `Enter the person's name (${NOME_MIN} to ${NOME_MAX} characters).`;
  }
  if (!chatChecked.size) {
    errs.chat_ids = 'Tick at least one person to receive the alerts.';
  } else if (![...chatChecked].every((id) => CHAT_RE.test(id))) {
    errs.chat_ids = 'One of the chosen chats has an invalid number.';
  }
  const msgLen = fMsg.value.trim().length;
  if (msgLen < MSG_MIN) errs.mensagem = `Write the alert message (at least ${MSG_MIN} characters).`;
  else if (msgLen > MSG_MAX) errs.mensagem = `The message is too long (limit ${MSG_MAX} characters).`;
  if (!MAC_RE.test(editing.mac)) errs.mac = 'This belt address is not valid.';
  return errs;
}

function setFieldError(name, inputEl, errEl, errs) {
  const show = touched.has(name) && errs[name];
  inputEl.closest('.field').classList.toggle('invalid', Boolean(show));
  errEl.textContent = show ? errs[name] : '';
  errEl.hidden = !show;
  inputEl.setAttribute('aria-invalid', show ? 'true' : 'false');
}

function validate() {
  const errs = fieldErrors();
  setFieldError('nome', fNome, $('f-nome-err'), errs);
  setFieldError('chat_ids', $('f-chats'), $('f-chat-err'), errs);
  setFieldError('mensagem', fMsg, $('f-msg-err'), errs);

  const valid = Object.keys(errs).length === 0;
  saveBtn.disabled = !valid;
  testBtn.disabled = chatChecked.size === 0;
  return valid;
}

// Counter and preview

function updateCounter() {
  const len = fMsg.value.trim().length;
  const c = $('f-msg-count');
  c.textContent = `${len} / ${MSG_MAX}`;
  c.classList.toggle('over', len > MSG_MAX || (len > 0 && len < MSG_MIN));
}

function updatePreview() {
  const raw = fMsg.value;
  let out = raw;
  for (const [key, val] of Object.entries(PLACEHOLDERS)) {
    out = out.split(key).join(val);
  }
  $('f-msg-preview').textContent = out || '—';

  const warn = $('f-msg-phwarn');
  const unknown = (raw.match(/\{[^{}]*\}/g) || []).filter((t) => !(t in PLACEHOLDERS));
  const strayBrace = out.includes('{') || out.includes('}');
  if (unknown.length) {
    warn.textContent = `Unknown placeholder: ${[...new Set(unknown)].join(', ')}. Check the spelling.`;
    warn.hidden = false;
  } else if (strayBrace) {
    warn.textContent = "There's a { or } that isn't part of a placeholder. Check the message.";
    warn.hidden = false;
  } else {
    warn.textContent = '';
    warn.hidden = true;
  }
}

// Insert placeholder at cursor

function insertAtCursor(el, text) {
  const start = el.selectionStart ?? el.value.length;
  const end = el.selectionEnd ?? el.value.length;
  el.value = el.value.slice(0, start) + text + el.value.slice(end);
  const pos = start + text.length;
  el.setSelectionRange(pos, pos);
  el.focus();
  el.dispatchEvent(new Event('input', { bubbles: true }));
}

// Save

function collect() {
  return {
    nome: fNome.value.trim(),
    mensagem: fMsg.value.trim(),
    chat_ids: [...chatChecked],
  };
}

async function onSubmit(ev) {
  ev.preventDefault();
  ['nome', 'chat_ids', 'mensagem'].forEach((n) => touched.add(n));
  if (!validate()) {
    form.querySelector('.field.invalid input, .field.invalid textarea')?.focus();
    return;
  }
  saveBtn.disabled = true;
  const state = $('f-save-state');
  state.hidden = false;
  state.textContent = 'Saving…';
  try {
    await api('PUT', `/api/dispositivos/${encodeURIComponent(editing.mac)}`, collect());
    formDirty = false;
    toast('Saved.');
    loadHome();
  } catch (e) {
    state.hidden = true;
    saveBtn.disabled = false;
    const w = $('form-warning');
    // These replies already say what to do; other errors get the generic prefix.
    const selfExplaining = e.code === 'cinto_sem_resposta' || e.code === 'cinto_nao_pendente';
    w.textContent = selfExplaining ? e.message : `Couldn't save: ${e.message}. Try again.`;
    w.hidden = false;
  }
}

// Exit form

async function attemptLeave() {
  if (formDirty) {
    const ok = await askConfirm('Discard your changes to this belt?', 'Discard');
    if (!ok) return;
  }
  formDirty = false;
  loadHome();
}

// Event listeners

form.addEventListener('submit', onSubmit);
$('form-back').addEventListener('click', attemptLeave);
$('f-cancel').addEventListener('click', attemptLeave);
$('home-retry').addEventListener('click', () => loadHome({ initial: true }));
findBtn.addEventListener('click', findChats);
testBtn.addEventListener('click', sendTest);

$('open-settings').addEventListener('click', openSettings);
$('settings-back').addEventListener('click', () => loadHome());
$('wifi-scan').addEventListener('click', scanNetworks);
$('wifi-connect').addEventListener('click', connectWifi);
$('tg-token').addEventListener('input', onTokenInput);
$('tg-test').addEventListener('click', testToken);
$('tg-save').addEventListener('click', saveToken);

form.addEventListener('input', () => { formDirty = true; });

fNome.addEventListener('input', validate);
fMsg.addEventListener('input', () => { updateCounter(); updatePreview(); validate(); });

[[fNome, 'nome'], [fMsg, 'mensagem']]
  .forEach(([el, name]) => el.addEventListener('blur', () => { touched.add(name); validate(); }));

$('f-chat-help-toggle').addEventListener('click', (e) => {
  const body = $('f-chat-help');
  const open = body.hidden;
  body.hidden = !open;
  e.currentTarget.setAttribute('aria-expanded', String(open));
});

for (const chip of document.querySelectorAll('.chip')) {
  chip.addEventListener('click', () => insertAtCursor(fMsg, chip.dataset.ph));
}

window.addEventListener('beforeunload', (e) => {
  if (formDirty) { e.preventDefault(); e.returnValue = ''; }
});

document.addEventListener('visibilitychange', () => {
  if (!document.hidden && !$('view-home').hidden) refreshPending();
});

setInterval(() => {
  if (!document.hidden && !$('view-home').hidden) refreshPending();
}, 3000);

// Start

// Open settings directly if Wi-Fi or token is unconfigured.
async function start() {
  try {
    const status = await api('GET', '/api/status');
    if (!status.wifi.conectado || !status.telegram.configurado) {
      showView('settings');
      applyStatus(status);
      return;
    }
  } catch (e) { // Fall back to home view
  }
  loadHome({ initial: true });
}

start();
