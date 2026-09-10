/* mock.js — dados falsos para desenvolver e demonstrar a interface sem o firmware.
   NÃO é copiado para firmware-master/data/. Só é carregado quando MOCK = true. */

const LATENCY = 300;

const now = Date.now();
const iso = (msAgo) => new Date(now - msAgo).toISOString();

let dispositivos = [
  {
    mac: 'A0:B7:65:2C:1D:E4',
    nome_idoso: 'Maria Aparecida',
    apelido: "Grandma Maria's belt",
    telegram_chat_id: '987654321',
    mensagem: 'ALERT: {nome} may have fallen. Detected at {hora} on {data}.',
    ativo: true,
    pareado_em: iso(1000 * 60 * 60 * 26),
  },
  {
    mac: '34:85:18:0A:9F:02',
    nome_idoso: 'João Batista',
    apelido: '',
    telegram_chat_id: '-1001445588',
    mensagem: 'Fall alert for {nome} at {hora}. Battery {bateria}.',
    ativo: false,
    pareado_em: iso(1000 * 60 * 60 * 72),
  },
];

let pendentes = [
  { mac: 'C8:F0:9E:11:74:BB', recebido_em: iso(1000 * 60 * 4) },
];

/* Depois de ~15 s aparece um novo pendente, simulando o botão de pareamento do cinto. */
setTimeout(() => {
  if (!pendentes.some((p) => p.mac === '9C:9C:1F:3D:20:57')
      && !dispositivos.some((d) => d.mac === '9C:9C:1F:3D:20:57')) {
    pendentes.push({ mac: '9C:9C:1F:3D:20:57', recebido_em: new Date().toISOString() });
  }
}, 15000);

/* teste-telegram percorre os cinco resultados possíveis a cada chamada. */
const TEST_CYCLE = ['ok', 'chat_desconhecido', 'sem_token', 'sem_internet', 'chat_id_invalido'];
let testIndex = 0;

/* PUT confirma com o cinto quase sempre; de vez em quando falha, para exercitar o aviso. */
let putCount = 0;

const config = () => ({
  versao: 1,
  atualizado_em: new Date().toISOString(),
  dispositivos,
});

function delay(value) {
  return new Promise((resolve) => setTimeout(() => resolve(value), LATENCY));
}

function macFromPath(path, prefix) {
  return decodeURIComponent(path.slice(prefix.length)).toUpperCase();
}

export function mockApi(method, path, body) {
  // GET /api/dispositivos
  if (method === 'GET' && path === '/api/dispositivos') {
    return delay({ dispositivos: dispositivos.map((d) => ({ ...d })) });
  }

  // GET /api/pendentes
  if (method === 'GET' && path === '/api/pendentes') {
    const macs = new Set(dispositivos.map((d) => d.mac));
    pendentes = pendentes.filter((p) => !macs.has(p.mac));
    return delay({ pendentes: pendentes.map((p) => ({ ...p })) });
  }

  // GET /api/config.json
  if (method === 'GET' && path === '/api/config.json') {
    return delay(config());
  }

  // PUT /api/dispositivos/{mac}
  if (method === 'PUT' && path.startsWith('/api/dispositivos/')) {
    const mac = macFromPath(path, '/api/dispositivos/');
    const device = { ...body, mac };
    const i = dispositivos.findIndex((d) => d.mac === mac);
    if (i >= 0) dispositivos[i] = device;
    else dispositivos.push(device);
    pendentes = pendentes.filter((p) => p.mac !== mac);
    const confirmado = (++putCount % 4) !== 0;
    // eslint-disable-next-line no-console
    console.log('[mock] config salva:\n' + JSON.stringify(config(), null, 2));
    return delay({ ok: true, confirmado_pelo_cinto: confirmado });
  }

  // DELETE /api/dispositivos/{mac}
  if (method === 'DELETE' && path.startsWith('/api/dispositivos/')) {
    const mac = macFromPath(path, '/api/dispositivos/');
    dispositivos = dispositivos.filter((d) => d.mac !== mac);
    return delay({ ok: true });
  }

  // DELETE /api/pendentes/{mac}
  if (method === 'DELETE' && path.startsWith('/api/pendentes/')) {
    const mac = macFromPath(path, '/api/pendentes/');
    pendentes = pendentes.filter((p) => p.mac !== mac);
    return delay({ ok: true });
  }

  // POST /api/teste-telegram
  if (method === 'POST' && path === '/api/teste-telegram') {
    const chat = (body && body.chat_id) || '';
    if (!/^-?\d{6,15}$/.test(chat)) {
      return delay({ ok: false, erro: 'chat_id_invalido', mensagem: 'Formato inválido.' });
    }
    const outcome = TEST_CYCLE[testIndex++ % TEST_CYCLE.length];
    if (outcome === 'ok') return delay({ ok: true });
    return delay({ ok: false, erro: outcome, mensagem: outcome });
  }

  return delay({ ok: false, erro: 'rota_desconhecida', mensagem: `Sem mock para ${method} ${path}` });
}
