// Mock API responses for testing UI without master firmware (loaded via ?mock).

const LATENCY = 300;

let wifi = { ssid: '', conectado: false, ip: '', endereco: 'cintoalerta.local', senha_incorreta: false };
let telegramToken = '';

let dispositivos = [
  {
    mac: 'A0:B7:65:2C:1D:E4',
    nome: 'Maria Aparecida',
    mensagem: 'ALERT: {nome} may have fallen. Detected at {hora} on {data}.',
    chat_ids: ['987654321'],
  },
];

// Belts asking to pair. The first keeps repeating its request (every GET shows it as
// "just now") and confirms on Save; the second stopped answering, so Save on it fails
// with "the belt did not answer".
const SILENT_MAC = 'F4:12:FA:9C:33:00';
let pendentes = [
  { mac: 'F4:12:FA:9C:33:08', visto: Date.now() },
  { mac: SILENT_MAC, visto: Date.now() - 4 * 60 * 1000 },
];

// Mock chat conversations from getUpdates.
const conversas = [
  { chat_id: '987654321', nome: 'Maria Aparecida' },
  { chat_id: '123456789', nome: 'Arthur Heberle' },
  { chat_id: '-1001445588', nome: 'Family group' },
];

// Alerts received (oldest first). Maria's belt raises a new one every 30 s; each
// is "sending" for 6 s, then delivered. Older ones cover the other outcomes.
const MOCK_START = Date.now();
let nextMockAlert = 1;
let alertas = [
  { id: 'A0B7652C1DE4-1', mac: 'A0:B7:65:2C:1D:E4', nome: 'Maria Aparecida', at: MOCK_START - 50 * 60 * 1000,
    conversas: [{ chat_id: '987654321', situacao: 'enviado', http: 200 },
                { chat_id: '-1001445588', situacao: 'recusado', http: 403 }] },
  { id: 'A0B7652C1DE4-2', mac: 'A0:B7:65:2C:1D:E4', nome: 'Maria Aparecida', at: MOCK_START - 12 * 60 * 1000,
    conversas: [{ chat_id: '987654321', situacao: 'enviado', http: 200 }] },
  { id: 'C8F09E1122AB-1', mac: 'C8:F0:9E:11:22:AB', nome: 'José Carlos', at: MOCK_START - 2 * 60 * 1000,
    conversas: [{ chat_id: '123456789', situacao: 'tentando_novamente', http: -1 }] },
  { id: 'D4E5F6070809-1', mac: 'D4:E5:F6:07:08:09', nome: 'D4:E5:F6:07:08:09', at: MOCK_START - 60 * 1000,
    naoCadastrado: true, conversas: [] },
];
// Delivered a few seconds after arriving.
for (const a of alertas) {
  for (const c of a.conversas) if (c.situacao === 'enviado') c.entregue_em = new Date(a.at + 4000).toISOString();
}

function mockAlertStatus(a) {
  if (a.naoCadastrado) return 'cinto_nao_cadastrado';
  if (!a.conversas.length) return 'sem_destinatarios';
  const st = a.conversas.map((c) => c.situacao);
  if (st.includes('tentando_novamente')) return 'tentando_novamente';
  if (st.includes('enviando')) return 'enviando';
  if (st.every((s) => s === 'enviado')) return 'enviado';
  return st.includes('enviado') ? 'parcial' : 'falhou';
}

function tickMockAlerts() {
  const now = Date.now();
  if (now - MOCK_START >= nextMockAlert * 30000) {
    alertas.push({ id: `A0B7652C1DE4-n${nextMockAlert}`, mac: 'A0:B7:65:2C:1D:E4', nome: 'Maria Aparecida',
      at: now, conversas: [{ chat_id: '987654321', situacao: 'enviando', http: 0 }] });
    nextMockAlert++;
  }
  for (const a of alertas) {
    if (now - a.at > 6000) {
      for (const c of a.conversas) {
        if (c.situacao === 'enviando') {
          c.situacao = 'enviado';
          c.http = 200;
          c.entregue_em = new Date(now).toISOString();
        }
      }
    }
  }
}

function delay(value) {
  return new Promise((resolve) => setTimeout(() => resolve(value), LATENCY));
}

function macFromPath(path, prefix) {
  return decodeURIComponent(path.slice(prefix.length)).toUpperCase();
}

const fail = (erro, mensagem) => ({ ok: false, erro, mensagem });

// Non-2xx reply, as the master sends: the app's api() throws it with .code set.
function reject(code, message) {
  return new Promise((_, rej) => setTimeout(() => {
    const err = new Error(message);
    err.code = code;
    rej(err);
  }, LATENCY));
}

// Telegram calls require active Wi-Fi and configured token.
function telegramPrecondition(token) {
  if (!token) return fail('sem_token', 'No token.');
  if (!wifi.conectado) return fail('sem_internet', 'Not connected.');
  return null;
}

export function mockApi(method, path, body) {
  // GET /api/status
  if (method === 'GET' && path === '/api/status') {
    return delay({
      wifi: { ...wifi },
      telegram: { configurado: Boolean(telegramToken) },
      relogio_ok: true,
    });
  }

  // GET /api/wifi/redes
  if (method === 'GET' && path === '/api/wifi/redes') {
    return delay({
      redes: [
        { ssid: 'Casa-2G', rssi: -48, aberta: false },
        { ssid: 'Vizinho', rssi: -71, aberta: false },
        { ssid: 'Cafe-Livre', rssi: -80, aberta: true },
      ],
    });
  }

  // POST /api/wifi — '12345678' simulates wrong password.
  if (method === 'POST' && path === '/api/wifi') {
    const wrong = body.senha === '12345678';
    wifi = wrong
      ? { ssid: body.ssid, conectado: false, ip: '', endereco: wifi.endereco, senha_incorreta: true }
      : { ssid: body.ssid, conectado: true, ip: '192.168.0.42', endereco: wifi.endereco, senha_incorreta: false };
    return delay({ ok: true });
  }

  // DELETE /api/wifi
  if (method === 'DELETE' && path === '/api/wifi') {
    wifi = { ssid: '', conectado: false, ip: '', endereco: wifi.endereco, senha_incorreta: false };
    return delay({ ok: true });
  }

  // POST /api/telegram/testar-token — tokens with ':' succeed; 'bad:...' fails.
  if (method === 'POST' && path === '/api/telegram/testar-token') {
    const token = (body && body.token) || '';
    const pre = telegramPrecondition(token);
    if (pre) return delay(pre);
    if (!token.includes(':') || token.startsWith('bad')) {
      return delay(fail('token_invalido', 'Invalid token.'));
    }
    return delay({ ok: true, bot: '@CintoAlertaBot' });
  }

  // PUT /api/telegram/token
  if (method === 'PUT' && path === '/api/telegram/token') {
    telegramToken = body.token;
    return delay({ ok: true });
  }

  // GET /api/telegram/conversas
  if (method === 'GET' && path === '/api/telegram/conversas') {
    const pre = telegramPrecondition(telegramToken);
    return delay(pre || { ok: true, conversas: conversas.map((c) => ({ ...c })) });
  }

  // POST /api/teste-telegram — group chat simulates delivery rejection.
  if (method === 'POST' && path === '/api/teste-telegram') {
    const pre = telegramPrecondition(telegramToken);
    if (pre) return delay(pre);
    if ((body && body.chat_id) === '-1001445588') {
      return delay(fail('chat_desconhecido', 'Rejected.'));
    }
    return delay({ ok: true });
  }

  // GET /api/dispositivos
  if (method === 'GET' && path === '/api/dispositivos') {
    return delay({ dispositivos: dispositivos.map((d) => ({ ...d, chat_ids: [...d.chat_ids] })) });
  }

  // PUT /api/dispositivos/{mac}
  if (method === 'PUT' && path.startsWith('/api/dispositivos/')) {
    const mac = macFromPath(path, '/api/dispositivos/');
    const device = { ...body, mac };
    const i = dispositivos.findIndex((d) => d.mac === mac);
    const isNew = i < 0;
    if (isNew) {
      // A new belt is saved only if it asked to pair and confirms the PairAccept.
      if (!pendentes.some((p) => p.mac === mac)) {
        return reject('cinto_nao_pendente',
          'This belt is not waiting to be set up. Press its pairing button.');
      }
      if (mac === SILENT_MAC) {
        return reject('cinto_sem_resposta',
          'The belt did not answer. Press its pairing button and try again.');
      }
      pendentes = pendentes.filter((p) => p.mac !== mac);
      dispositivos.push(device);
    } else {
      dispositivos[i] = device;
    }
    // eslint-disable-next-line no-console
    console.log('[mock] cinto salvo:\n' + JSON.stringify(device, null, 2));
    return delay({ ok: true, confirmado_pelo_cinto: isNew });
  }

  // DELETE /api/dispositivos/{mac}
  if (method === 'DELETE' && path.startsWith('/api/dispositivos/')) {
    const mac = macFromPath(path, '/api/dispositivos/');
    dispositivos = dispositivos.filter((d) => d.mac !== mac);
    return delay({ ok: true });
  }

  // GET /api/pendentes
  if (method === 'GET' && path === '/api/pendentes') {
    const repeating = pendentes.find((p) => p.mac !== SILENT_MAC);
    if (repeating) repeating.visto = Date.now();
    return delay({
      pendentes: pendentes.map((p) => ({ mac: p.mac, recebido_em: new Date(p.visto).toISOString() })),
    });
  }

  // DELETE /api/pendentes/{mac}
  if (method === 'DELETE' && path.startsWith('/api/pendentes/')) {
    const mac = macFromPath(path, '/api/pendentes/');
    pendentes = pendentes.filter((p) => p.mac !== mac);
    return delay({ ok: true });
  }

  // GET /api/alertas — newest first
  if (method === 'GET' && path === '/api/alertas') {
    tickMockAlerts();
    return delay({
      alertas: alertas.slice().reverse().map((a) => ({
        id: a.id, mac: a.mac, nome: a.nome, recebido_em: new Date(a.at).toISOString(),
        situacao: mockAlertStatus(a), conversas: a.conversas.map((c) => ({ ...c })),
      })),
    });
  }

  // DELETE /api/alertas (all) and /api/alertas/{id}
  if (method === 'DELETE' && path === '/api/alertas') {
    alertas = [];
    return delay({ ok: true });
  }
  if (method === 'DELETE' && path.startsWith('/api/alertas/')) {
    const id = decodeURIComponent(path.slice('/api/alertas/'.length));
    alertas = alertas.filter((a) => a.id !== id);
    return delay({ ok: true });
  }

  return delay(fail('rota_desconhecida', `Sem mock para ${method} ${path}`));
}
