#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <functional>

#include "Domain/SystemConfig.h"
#include "Storage/ConfigStorage.h"

// Pagina de configuracao da master: serve o frontend gravado no LittleFS e a API
// JSON que ele usa (Wi-Fi, token do Telegram, cintos). Roda inteira na task do
// loop(), entao compartilha `config` com o AppController sem concorrencia.
// As rotas que falam com o Telegram bloqueiam o loop() por alguns segundos.
class WebPortal {
public:
    // `onWifiChanged` e chamado depois de gravar um novo SSID/senha, para reconectar.
    WebPortal(SystemConfig& config, ConfigStorage& configStorage,
              std::function<void()> onWifiChanged);

    void begin();   // registra as rotas e liga o servidor na porta 80
    void handle();  // atende as requisicoes pendentes; chamar a cada loop()

private:
    void serveFile(const char* path, const char* contentType);

    // Resposta {ok:false, erro, mensagem}. Resultados esperados do Telegram (token
    // invalido, sem internet...) usam code 200, para o frontend trata-los como resultado.
    void sendJson(int code, const JsonDocument& doc);
    void sendError(int code, const char* error, const String& message);
    bool readBody(JsonDocument& doc);

    // Verifica o que o Telegram exige (token, Wi-Fi, relogio). Se faltar algo,
    // responde com o erro e retorna false.
    bool requireTelegram(const String& token);

    void handleStatus();
    void handleWifiNetworks();
    void handleWifiSave();
    void handleTokenTest();
    void handleTokenSave();
    void handleChats();
    void handleChatTest();
    void handleDevicesList();
    void handleDeviceSave();
    void handleDeviceDelete();

    WebServer server{80};
    SystemConfig& config;
    ConfigStorage& configStorage;
    std::function<void()> onWifiChanged;
};
