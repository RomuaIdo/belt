const MOCK = true;
const API_BASE = '';

/* Em MOCK, todas as chamadas são atendidas por mock.js (não vai para o firmware). */
let mockApi = null;
if (MOCK) {
  ({ mockApi } = await import('./mock.js'));
}

/* ---------- regras de domínio ---------- */

const CHAT_RE = /^-?\d{6,15}$/;
const MAC_RE = /^([0-9A-F]{2}:){5}[0-9A-F]{2}$/;
const NOME_MIN = 2, NOME_MAX = 40;
const APELIDO_MAX = 30;
const MSG_MIN = 10, MSG_MAX = 300;

const DEFAULT_MSG = 'ALERT: {nome} may have fallen. Detected at {hora} on {data}.';

const PLACEHOLDERS = {
  '{nome}': 'Maria Aparecida',
  '{apelido}': "Grandma Maria's belt",
  '{hora}': '14:32',
  '{data}': '09/09/2026',
  '{bateria}': '41%',
};

const TEST_MESSAGES = {
  ok: ['Test message sent. Check Telegram.', 'ok'],
  chat_id_invalido: ['Invalid format. It must be a number, with no spaces.', 'bad'],
  chat_desconhecido: ['Telegram rejected it. The person needs to send a message to the bot before they can receive alerts.', 'bad'],
  sem_token: ['The master does not yet have the Telegram bot configured.', 'bad'],
  sem_internet: ['The master has no internet connection. Testing is only possible after configuring Wi-Fi.', 'bad'],
};

/* ---------- camada de API ---------- */

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

/* ---------- utilidades ---------- */

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

let toastTimer = null;
function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}

/* ---------- diálogo de confirmação ---------- */

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

/* ---------- troca de telas ---------- */

function showView(name) {
  $('view-home').hidden = name !== 'home';
  $('view-form').hidden = name !== 'form';
  window.scrollTo(0, 0);
}

/* ================= TELA INICIAL ================= */

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
    const devices = dev.dispositivos || [];
    configuredMacs = new Set(devices.map((d) => d.mac.toUpperCase()));
    const pendentes = (pend.pendentes || [])
      .filter((p) => !configuredMacs.has(p.mac.toUpperCase()));

    clearHomeStatus();

    if (!devices.length && !pendentes.length) {
      setHomeStatus('No belts yet. Press the pairing button on a belt and it will show up here.');
      knownPendingMacs = new Set();
      return;
    }

    renderDevices(devices);
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
  const mac = d.mac.toUpperCase();
  return h('li', {},
    h('div', { class: 'row-main' },
      h('span', { class: 'dev-name', text: d.nome_idoso }),
      d.apelido ? h('span', { class: 'dev-alias', text: d.apelido }) : null,
      h('span', { class: 'state-dot' + (d.ativo ? '' : ' is-off'), text: d.ativo ? 'Active' : 'Inactive' }),
    ),
    h('p', { class: 'mac mac-sm', text: mac }),
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
  const ok = await askConfirm(`Remove ${d.nome_idoso}'s belt? Alerts from it will stop.`, 'Remove');
  if (!ok) return;
  try {
    await api('DELETE', `/api/dispositivos/${encodeURIComponent(d.mac)}`);
    toast(`${d.nome_idoso}'s belt was removed.`);
    loadHome();
  } catch (e) {
    toast("Couldn't remove it. Try again.");
  }
}

/* ---------- polling da lista de pendentes ---------- */

async function refreshPending() {
  if (!$('view-form').hidden) return;
  try {
    const pend = await api('GET', '/api/pendentes');
    const pendentes = (pend.pendentes || [])
      .filter((p) => !configuredMacs.has(p.mac.toUpperCase()));

    // se a tela está em estado vazio/erro e agora chegou algo, recarrega tudo
    if (!$('home-status').hidden) {
      if (pendentes.length) loadHome();
      return;
    }
    renderPending(pendentes, { animateNew: true });
    knownPendingMacs = new Set(pendentes.map((p) => p.mac.toUpperCase()));
  } catch (e) {
    /* falha de polling é silenciosa: não destrói a tela */
  }
}

/* ================= FORMULÁRIO ================= */

const form = $('device-form');
let editing = null;      // dispositivo em edição
let isNewPairing = false;
let formDirty = false;
const touched = new Set();

const fMac = $('f-mac');
const fNome = $('f-nome');
const fApelido = $('f-apelido');
const fChat = $('f-chat');
const fMsg = $('f-msg');
const fAtivo = $('f-ativo');
const saveBtn = $('f-save');
const testBtn = $('f-test');

function startSetup(p) {
  editing = {
    mac: p.mac.toUpperCase(),
    nome_idoso: '',
    apelido: '',
    telegram_chat_id: '',
    mensagem: DEFAULT_MSG,
    ativo: true,
    pareado_em: null,
  };
  isNewPairing = true;
  $('form-title').textContent = 'Set up belt';
  openForm();
}

function startEdit(d) {
  editing = { ...d, mac: d.mac.toUpperCase() };
  isNewPairing = false;
  $('form-title').textContent = 'Edit belt';
  openForm();
}

function openForm() {
  fMac.textContent = editing.mac;
  fNome.value = editing.nome_idoso || '';
  fApelido.value = editing.apelido || '';
  fChat.value = editing.telegram_chat_id || '';
  fMsg.value = editing.mensagem || '';
  fAtivo.checked = editing.ativo !== false;

  touched.clear();
  formDirty = false;
  $('form-warning').hidden = true;
  $('f-save-state').hidden = true;
  $('f-test-result').textContent = '';
  $('f-test-result').className = 'test-result';
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

/* ---------- validação ---------- */

function fieldErrors() {
  const errs = {};
  const nome = fNome.value.trim();
  if (nome.length < NOME_MIN || nome.length > NOME_MAX) {
    errs.nome_idoso = `Enter the person's name (${NOME_MIN} to ${NOME_MAX} characters).`;
  }
  if (fApelido.value.trim().length > APELIDO_MAX) {
    errs.apelido = `Keep the nickname under ${APELIDO_MAX} characters.`;
  }
  const chat = fChat.value.trim();
  if (!chat) errs.telegram_chat_id = 'Enter the Telegram recipient number.';
  else if (!CHAT_RE.test(chat)) {
    errs.telegram_chat_id = 'It must be 6 to 15 digits, no spaces. See "How do I get this number?".';
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
  setFieldError('nome_idoso', fNome, $('f-nome-err'), errs);
  setFieldError('apelido', fApelido, $('f-apelido-err'), errs);
  setFieldError('telegram_chat_id', fChat, $('f-chat-err'), errs);
  setFieldError('mensagem', fMsg, $('f-msg-err'), errs);

  const valid = Object.keys(errs).length === 0;
  saveBtn.disabled = !valid;
  testBtn.disabled = !CHAT_RE.test(fChat.value.trim());
  return valid;
}

/* ---------- contador e pré-visualização da mensagem ---------- */

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

/* ---------- inserir placeholder no cursor ---------- */

function insertAtCursor(el, text) {
  const start = el.selectionStart ?? el.value.length;
  const end = el.selectionEnd ?? el.value.length;
  el.value = el.value.slice(0, start) + text + el.value.slice(end);
  const pos = start + text.length;
  el.setSelectionRange(pos, pos);
  el.focus();
  el.dispatchEvent(new Event('input', { bubbles: true }));
}

/* ---------- teste do Telegram ---------- */

function showTestResult(code) {
  const [msg, kind] = TEST_MESSAGES[code] || TEST_MESSAGES.chat_desconhecido;
  const el = $('f-test-result');
  el.textContent = msg;
  el.className = 'test-result ' + (kind === 'ok' ? 'ok' : 'bad');
}

async function sendTest() {
  const chat = fChat.value.trim();
  if (!CHAT_RE.test(chat)) { showTestResult('chat_id_invalido'); return; }
  testBtn.disabled = true;
  const el = $('f-test-result');
  el.textContent = 'Sending…';
  el.className = 'test-result';
  try {
    const res = await api('POST', '/api/teste-telegram', { chat_id: chat });
    showTestResult(res.ok ? 'ok' : (res.erro || 'chat_desconhecido'));
  } catch (e) {
    showTestResult('sem_internet');
  } finally {
    testBtn.disabled = !CHAT_RE.test(fChat.value.trim());
  }
}

/* ---------- salvar ---------- */

function collect() {
  return {
    mac: editing.mac,
    nome_idoso: fNome.value.trim(),
    apelido: fApelido.value.trim(),
    telegram_chat_id: fChat.value.trim(),
    mensagem: fMsg.value.trim(),
    ativo: fAtivo.checked,
    pareado_em: editing.pareado_em || new Date().toISOString(),
  };
}

async function onSubmit(ev) {
  ev.preventDefault();
  ['nome_idoso', 'apelido', 'telegram_chat_id', 'mensagem'].forEach((n) => touched.add(n));
  if (!validate()) {
    form.querySelector('.field.invalid input, .field.invalid textarea')?.focus();
    return;
  }
  const device = collect();
  saveBtn.disabled = true;
  const state = $('f-save-state');
  state.hidden = false;
  state.textContent = 'Confirming with the belt…';
  try {
    const res = await api('PUT', `/api/dispositivos/${encodeURIComponent(device.mac)}`, device);
    formDirty = false;
    toast(res.confirmado_pelo_cinto === false
      ? "Saved. The belt didn't confirm — it may be off or out of range."
      : 'Saved.');
    loadHome();
  } catch (e) {
    state.hidden = true;
    saveBtn.disabled = false;
    const w = $('form-warning');
    w.textContent = `Couldn't save: ${e.message}. Try again.`;
    w.hidden = false;
  }
}

/* ---------- sair do formulário ---------- */

async function attemptLeave() {
  if (formDirty) {
    const ok = await askConfirm('Discard your changes to this belt?', 'Discard');
    if (!ok) return;
  }
  formDirty = false;
  loadHome();
}

/* ================= LISTENERS ================= */

form.addEventListener('submit', onSubmit);
$('form-back').addEventListener('click', attemptLeave);
$('f-cancel').addEventListener('click', attemptLeave);
$('home-retry').addEventListener('click', () => loadHome({ initial: true }));
testBtn.addEventListener('click', sendTest);

form.addEventListener('input', () => { formDirty = true; });

fNome.addEventListener('input', validate);
fApelido.addEventListener('input', validate);
fChat.addEventListener('input', validate);
fMsg.addEventListener('input', () => { updateCounter(); updatePreview(); validate(); });

[[fNome, 'nome_idoso'], [fApelido, 'apelido'], [fChat, 'telegram_chat_id'], [fMsg, 'mensagem']]
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
  if (!document.hidden) refreshPending();
});

setInterval(() => {
  if (!document.hidden && !$('view-home').hidden) refreshPending();
}, 3000);

/* ================= START ================= */

loadHome({ initial: true });
