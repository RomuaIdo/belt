#pragma once

#include <memory>
#include <vector>

#include "Domain/Notification.h"
#include "Domain/SystemConfig.h"
#include "Messaging/TelegramTask.h"
#include "Storage/CallQueueStorage.h"
#include "Storage/ConfigStorage.h"
#include "Web/WebPortal.h"

class AppController {
public:
    explicit AppController(const String& configFilePath);

    // Monta o LittleFS, carrega a config e a fila de alertas pendentes, cria a rede
    // Wi-Fi propria da master (pagina de configuracao) e conecta no Wi-Fi salvo, se
    // houver (sem bloquear). Retorna false se o LittleFS nao montar.
    bool setup();

    // Um passo: atende a pagina de configuracao, mantem Wi-Fi e relogio, coleta os
    // envios concluidos e inicia os pendentes. Chamar a cada loop(), apos setup().
    // Nao bloqueia, exceto quando a pagina pede algo ao Telegram (alguns segundos).
    void execute();

    // Registra um alerta do cinto `originMac`: grava na fila e o envio ocorre em
    // execute(). Retorna false se o MAC for desconhecido, nao houver destinatarios
    // ou a gravacao falhar (neste caso o alerta ainda e enviado a partir da RAM).
    // Chamar somente pela tarefa de loop(); um callback ESP-NOW deve repassar o MAC
    // por uma fila FreeRTOS.
    bool enqueueAlert(const String& originMac);

private:
    static constexpr size_t MAX_PARALLEL_SENDS = 2;

    void connectWifi();
    void maintainWifi();
    void collectFinishedSends();
    void dispatchPendingSends();
    bool isSending(const String& eventId, const String& chatId) const;
    void markChatAsSent(const String& eventId, const String& chatId);
    std::vector<Notification>::iterator findPending(const String& eventId);

    SystemConfig config;
    ConfigStorage configStorage;

    // Declarado depois de config e configStorage: guarda referencias a eles.
    WebPortal webPortal;

    // Criado em setup(), depois que o LittleFS estiver montado.
    std::unique_ptr<CallQueueStorage> callQueueStorage;

    // Espelho em RAM da fila gravada na flash.
    std::vector<Notification> pendingNotifications;

    // Array fixo: cada task guarda o proprio endereco, entao nao pode se mover.
    TelegramTask telegramTasks[MAX_PARALLEL_SENDS];

    // Backoff apos falha de envio. 0 = sem espera; dobra a cada falha ate o limite.
    uint32_t lastFailureMs = 0;
    uint32_t retryDelayMs = 0;

    uint32_t lastWifiAttemptMs = 0;
    bool ntpStarted = false;
};
