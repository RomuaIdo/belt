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

  return delay(fail('rota_desconhecida', `Sem mock para ${method} ${path}`));
}
