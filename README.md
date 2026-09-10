# Cinto Alerta

Detector e notificador de quedas para pessoas idosas. Dois cintos com ESP32-S3 e MPU-6050
classificam quedas com um modelo embarcado, avisam o usuário por vibração e LED e — se o
usuário não cancelar — enviam um alerta via ESP-NOW para uma estação master, que notifica um
cuidador pelo Telegram.

Projeto acadêmico de Oficinas de Integração 2 — Engenharia de Computação, UTFPR.
Equipe: Rafael de Andrade Fernandes, Arthur Gabriel Pellegrini Heberle, Vinícius Romualdo Silva.

## Arquitetura

```
2x CINTO (slave)            1x MASTER                    NOTIFICAÇÃO
ESP32-S3 Supermini         ESP32-S3 Dev Module          Telegram Bot API
MPU-6050                   ligado à tomada
LiPo 500 mAh      ESP-NOW           Wi-Fi / HTTPS
motor de vibração ───────►          ──────────►          celular do cuidador
botão(s)          (MAC, sem IP)     (REST + JSON)
```

O identificador de cada cinto é o **endereço MAC** — não há IP entre cinto e master.
Detalhes e decisões fechadas em [`docs/CONTEXTO.md`](docs/CONTEXTO.md).

## Estrutura do repositório

| Caminho | Conteúdo |
|---|---|
| `frontend/` | Interface de configuração do master (M1.1) — HTML/CSS/JS puro, servido do LittleFS |
| `master/main.cpp` | Firmware do master (protótipo: SoftAP, portal cativo, ESP-NOW, Telegram) |
| `docs/CONTEXTO.md` | Fonte única de verdade: arquitetura, hardware, cronograma, conflitos em aberto |
| `docs/CLAUDE.md` | Regras de trabalho e fatos técnicos que não podem ser contrariados |
| `docs/master/FRONTEND_CONFIG.md` | Especificação da interface de configuração e contrato da API |
| `.claude/skills/frontend-master/` | Regras e checklist para mexer no `frontend/` |

## Front end de configuração (`frontend/`)

Interface onde o cuidador associa cada cinto (identificado pelo MAC que aparece após o
pareamento) a um nome, uma mensagem de alerta e um destinatário no Telegram.

Restrições: sem recursos externos (o celular fica na SoftAP do master, sem internet), sem
framework, sem etapa de build, menos de 100 KB. Ver
[`docs/master/FRONTEND_CONFIG.md`](docs/master/FRONTEND_CONFIG.md).

### Rodar localmente

```bash
cd frontend
python -m http.server 8000    # ou: python3 -m http.server 8000
# abrir http://localhost:8000
```

`app.js` começa com `const MOCK = true`: todas as chamadas são atendidas por `mock.js`
(dados falsos, latência simulada, um cinto pendente novo aparece após ~15 s). `mock.js` é só
para desenvolvimento — **não** vai para `firmware-master/data/`. O deploy no LittleFS é
`index.html` + `style.css` + `app.js`.

## Firmware

O firmware do master está em `master/main.cpp`, hoje compilado pela Arduino IDE (biblioteca
ArduinoJson). O sistema de pareamento cinto↔master via ESP-NOW (M1.4) e o firmware do cinto
ainda não foram implementados.

## Cronograma

| Milestone | Tema | Prazo |
|---|---|---|
| M1 | Infraestrutura web e comunicação | 07/10/2026 |
| M2 | Eletrônica, energia e machine learning | 11/11/2026 |
| M3 | Prototipagem e testes finais | 25/11/2026 |
| M4 | Entrega final (relatório, vídeo, blog) | 02/12/2026 |
| M5 | Apresentação para a banca | 09/12/2026 |
