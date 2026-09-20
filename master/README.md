# Master

Projeto PlatformIO com framework Arduino para o ESP32-S3-DevKitC-1
(N16R8: 16 MB de flash e 8 MB de PSRAM).

## Organização

| Diretório | Responsabilidade |
|---|---|
| `include/App/` | Declaração do `AppController`, responsável pela futura coordenação do master |
| `include/Domain/` e `src/Domain/` | Modelos de configuração, dispositivos e notificações |
| `include/Messaging/` e `src/Messaging/` | Funções de criação do payload e envio pelo Telegram |
| `include/Storage/` e `src/Storage/` | Persistência da configuração e da fila no LittleFS |
| `include/Util/` | Utilitários compartilhados, como normalização de MAC |
| `src/main.cpp` | Entrada do firmware: `setup()` e `loop()` |
| [frontend/](frontend/) | Interface independente em HTML, CSS e JavaScript, ainda com dados simulados |
| [test/](test/) | Suítes Unity existentes, organizadas por componente |
| [prototypes/](prototypes/) | Experimentos independentes, fora da compilação do firmware |

Arquivos `.h` ficam em `include/` e suas implementações `.cpp` em `src/`,
com o mesmo subdiretório e nome-base. Por exemplo,
`include/Messaging/telegram_call.h` corresponde a `src/Messaging/telegram_call.cpp`.
Os includes usam o caminho a partir de `include/`: `Messaging/telegram_call.h`.
Utilitários implementados inteiramente no cabeçalho não precisam de um `.cpp`.

## Estado atual

Existem modelos de domínio, armazenamento em LittleFS, funções de Telegram e
testes desses componentes. O frontend continua com `MOCK = true` e não está
conectado ao firmware.

`AppController` possui apenas o cabeçalho. Faltam as definições do construtor e
de `execute()` que `src/main.cpp` utiliza; por isso, o firmware completo ainda
não pode ser vinculado. A futura implementação pertence a `src/App/AppController.cpp`.

Hospedagem da interface, APIs HTTP, configuração de Wi-Fi, pareamento ESP-NOW e
processamento da fila ainda dependem de integração. Não há diretório `data/` nem
empacotamento do frontend nesta etapa. Eles serão adicionados com a hospedagem.

## Compilar e testar

Execute a partir de `master/`, em um terminal com PlatformIO disponível.

```sh
# Compilação do firmware; atualmente bloqueada pelas definições de AppController.
pio run -e esp32-s3-devkitc-1

# Compilar a suíte de Telegram sem enviar ou executar na placa.
pio test -e esp32-s3-devkitc-1-test -f test_telegram_call --without-uploading --without-testing

# Enviar e executar uma suíte na placa conectada.
pio test -e esp32-s3-devkitc-1-test -f test_telegram_call
```

As outras suítes são `test_domain_models`, `test_notification`,
`test_config_storage`, `test_call_queue_storage` e `test_board_specs`.
Use o nome correspondente com `-f`. Os testes de persistência escrevem na flash;
os testes de reinicialização exigem uma placa e conexão serial compatíveis.

O ambiente de testes seleciona `Domain/`, `Storage/` e `Messaging/`, sem compilar
`src/main.cpp`. Código em `prototypes/` não entra em nenhum desses ambientes.

## Visualizar o frontend

A partir de `master/`:

```sh
python -m http.server 8000 --bind 127.0.0.1 --directory frontend
```

Abra <http://localhost:8000>. Os quatro arquivos permanecem juntos:
`index.html`, `style.css`, `app.js` e `mock.js`. O módulo de mock permite
experimentar a interface sem ESP32 ou chamadas reais ao Telegram.

Essa prévia local não grava configuração no master nem faz upload de arquivos.
