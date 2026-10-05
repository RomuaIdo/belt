/* mock.js — dados falsos para desenvolver e demonstrar a interface sem a master.
   Só é carregado quando a página é aberta com ?mock na URL. Espelha a API do firmware
   (src/Web/WebPortal.cpp). */

const LATENCY = 300;

let wifi = { ssid: '', conectado: false, ip: '' };
let telegramToken = '';

let dispositivos = [
  {
    mac: 'A0:B7:65:2C:1D:E4',
    nome: 'Maria Aparecida',
    mensagem: 'ALERT: {nome} may have fallen. Detected at {hora} on {data}.',
    chat_ids: ['987654321'],
  },
];

/* Quem já falou com o bot, como o getUpdates devolveria. */
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

/* Os resultados do Telegram só funcionam com Wi-Fi conectado e token salvo. */
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

  // POST /api/wifi — a "senha errada" é simulada com a senha 12345678.
  if (method === 'POST' && path === '/api/wifi') {
    const wrong = body.senha === '12345678';
    wifi = wrong
      ? { ssid: body.ssid, conectado: false, ip: '' }
      : { ssid: body.ssid, conectado: true, ip: '192.168.0.42' };
    return delay({ ok: true });
  }

  // POST /api/telegram/testar-token — qualquer token com ":" funciona; "bad:..." é recusado.
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

  // POST /api/teste-telegram — o grupo recusa, para exercitar o aviso de erro.
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
    if (i >= 0) dispositivos[i] = device;
    else dispositivos.push(device);
    // eslint-disable-next-line no-console
    console.log('[mock] cinto salvo:\n' + JSON.stringify(device, null, 2));
    return delay({ ok: true, confirmado_pelo_cinto: true });
  }

  // DELETE /api/dispositivos/{mac}
  if (method === 'DELETE' && path.startsWith('/api/dispositivos/')) {
    const mac = macFromPath(path, '/api/dispositivos/');
    dispositivos = dispositivos.filter((d) => d.mac !== mac);
    return delay({ ok: true });
  }

  // GET /api/pendentes — o pareamento ainda não existe no firmware
  if (method === 'GET' && path === '/api/pendentes') {
    return delay({ pendentes: [] });
  }

  // DELETE /api/pendentes/{mac}
  if (method === 'DELETE' && path.startsWith('/api/pendentes/')) {
    return delay({ ok: true });
  }

  return delay(fail('rota_desconhecida', `Sem mock para ${method} ${path}`));
}
