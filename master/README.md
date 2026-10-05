# Master

Projeto PlatformIO com framework Arduino para o ESP32-S3-DevKitC-1
(N16R8: 16 MB de flash e 8 MB de PSRAM).

## Organização

| Diretório | Responsabilidade |
|---|---|
| `include/App/` e `src/App/` | `AppController`, que coordena Wi-Fi, fila de alertas, envios ao Telegram e a página de configuração |
| `include/Domain/` e `src/Domain/` | Modelos de configuração, dispositivos e notificações |
| `include/Messaging/` e `src/Messaging/` | `TelegramTask` (envio pelo Telegram em uma task FreeRTOS) e formatação da mensagem de alerta |
| `include/Storage/` e `src/Storage/` | Persistência da configuração e da fila no LittleFS |
| `include/Web/` e `src/Web/` | `WebPortal`: servidor web da página de configuração e a API que ela usa |
| `include/Util/` | Utilitários compartilhados, como normalização de MAC e validade do relógio |
| `src/main.cpp` | Entrada do firmware: `setup()` e `loop()` |
| [frontend/](frontend/) | Página de configuração em HTML, CSS e JavaScript, gravada no LittleFS |
| [test/](test/) | Suítes Unity existentes, organizadas por componente |
| [prototypes/](prototypes/) | Experimentos independentes, fora da compilação do firmware |

Arquivos `.h` ficam em `include/` e suas implementações `.cpp` em `src/`,
com o mesmo subdiretório e nome-base. Por exemplo,
`include/Messaging/TelegramTask.h` corresponde a `src/Messaging/TelegramTask.cpp`.
Os includes usam o caminho a partir de `include/`: `Messaging/TelegramTask.h`.
Utilitários implementados inteiramente no cabeçalho não precisam de um `.cpp`.

## Estado atual

`AppController` (`src/App/AppController.cpp`):

- `setup()` monta o LittleFS, carrega a configuração, abre a fila em `/queue`
  (removendo arquivos inválidos), cria a rede Wi-Fi própria da master, liga o
  servidor web e conecta no Wi-Fi salvo (modo AP+STA).
- `execute()`, chamado por `loop()`, atende a página de configuração, mantém o Wi-Fi
  e o relógio (NTP, fuso UTC-3), coleta os envios concluídos e despacha os pendentes
  ao Telegram em tarefas FreeRTOS (no máximo 2 simultâneas). O certificado raiz fica
  em `include/Messaging/telegram_certificate.h`.
- `enqueueAlert(mac)` é o ponto de entrada para um alerta de cinto: formata a
  mensagem do peer, grava na fila e o envio ocorre em `execute()`. Falhas de rede e
  5xx/429 são retentadas com backoff de 5 s a 5 min; HTTP 400/403 descarta aquele chat.

Nada ainda chama `enqueueAlert()`: o recebimento ESP-NOW e o pareamento dependem de
integração. Por isso a lista "Waiting to be set up" da página fica sempre vazia, e os
cintos são cadastrados pelo botão "Add belt manually".

## Configurar a master pelo celular

A configuração (Wi-Fi, token do Telegram, cintos) fica no `/config.json` do LittleFS e
é carregada a cada boot: depois de configurada, a master volta a conectar sozinha.

**1. Gravar a página e o firmware** (placa na porta USB marcada `UART`; a partir de `master/`):

```sh
pio run -e esp32-s3-devkitc-1 -t uploadfs   # grava frontend/ no LittleFS
pio run -e esp32-s3-devkitc-1 -t upload     # grava o firmware
pio device monitor                           # serial a 115200
```

> `uploadfs` **apaga o LittleFS inteiro**, inclusive o `config.json` e a fila de alertas.
> Rode só quando a página (`frontend/`) mudar; para trocar apenas o firmware use `upload`.

No monitor deve aparecer `rede 'CintoAlerta-Master' criada; pagina em http://192.168.4.1`.

**2. Abrir a página.** No celular, conecte na rede Wi-Fi `CintoAlerta-Master`
(senha `cintoalerta`, definida em `src/App/AppController.cpp`) e abra
<http://192.168.4.1>. A rede da master fica sempre ligada, mesmo depois de ela
conectar no roteador. Sem Wi-Fi ou sem token, a página abre direto em **Settings**.

**3. Wi-Fi.** Em Settings, "Search networks", escolha a rede de **2,4 GHz** da casa
(o ESP32 não usa 5 GHz), digite a senha e "Connect". O celular pode perder o sinal da
master por 1 ou 2 s quando o canal muda.

**4. Telegram.** No Telegram, fale com `@BotFather`, envie `/newbot` e copie o token.
Na página, cole o token, "Test" (a master consulta o Telegram e mostra o nome do bot) e "Save".

**5. Cintos.** Na tela inicial, "Add belt manually" e digite o MAC. No formulário:
peça à pessoa que abra o bot no Telegram e aperte **Start**, depois use
**Find conversations** e marque quem recebe os alertas. **Send test** manda uma
mensagem de verdade para os marcados; **Save** grava no `config.json`.

Se alguém não aparecer em "Find conversations", o Telegram só guarda as mensagens por
24 h: peça para a pessoa mandar outra mensagem ao bot.

## Compilar e testar

Execute a partir de `master/`, em um terminal com PlatformIO disponível.

```sh
# Compilação do firmware completo.
pio run -e esp32-s3-devkitc-1

# Compilar a suíte de Telegram sem enviar ou executar na placa.
pio test -e esp32-s3-devkitc-1-test -f test_telegram_task --without-uploading --without-testing

# Enviar e executar uma suíte na placa conectada.
pio test -e esp32-s3-devkitc-1-test -f test_telegram_task
```

As outras suítes são `test_alert_message`, `test_domain_models`, `test_notification`,
`test_config_storage`, `test_call_queue_storage` e `test_board_specs`.
Use o nome correspondente com `-f`. Os testes de persistência escrevem na flash;
os testes de reinicialização exigem uma placa e conexão serial compatíveis.

O ambiente de testes seleciona `Domain/`, `Storage/` e `Messaging/`, sem compilar
`src/main.cpp`, `App/` nem `Web/`. Código em `prototypes/` não entra em nenhum desses ambientes.

## Visualizar o frontend sem a placa

A partir de `master/`:

```sh
python -m http.server 8000 --bind 127.0.0.1 --directory frontend
```

Abra <http://localhost:8000/?mock>. O `?mock` carrega `mock.js`, que simula a API do
firmware (`src/Web/WebPortal.cpp`) sem ESP32 nem chamadas reais ao Telegram. Sem o
`?mock`, a página fala com a API real. A master só serve `index.html`, `style.css` e
`app.js`; o `mock.js` vai para a flash junto com a pasta, mas não é entregue.

Essa prévia local não grava configuração no master.
