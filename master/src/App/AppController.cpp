#include "App/AppController.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

#include "Messaging/alert_message.h"
#include "Util/Clock.h"

namespace {
constexpr const char* QUEUE_DIR = "/queue";

// Rede propria da master, sempre ligada: a pagina fica em http://192.168.4.1.
constexpr const char* AP_SSID = "CintoAlerta-Master";
constexpr const char* AP_PASSWORD = "cintoalerta";  // WPA2 exige 8 ou mais caracteres

constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t RETRY_INITIAL_MS = 5000;
constexpr uint32_t RETRY_MAX_MS = 300000;

// Fuso de Curitiba (UTC-3, sem horario de verao) e servidores NTP.
constexpr const char* TIMEZONE = "<-03>3";
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.google.com";

bool isSuccess(int httpStatus) { return httpStatus >= 200 && httpStatus < 300; }

// O Telegram recusou o chat de forma definitiva (chat inexistente, bot bloqueado):
// retentar nao adianta.
bool isPermanentRejection(int httpStatus) { return httpStatus == 400 || httpStatus == 403; }
}

AppController::AppController(const String& configFilePath)
    : configStorage(configFilePath),
      webPortal(config, configStorage, [this] {
          // A pagina gravou outro Wi-Fi: largar a rede atual e conectar na nova.
          WiFi.disconnect();
          connectWifi();
      }) {}

bool AppController::setup() {
    if (!LittleFS.begin(false)) {
        return false;
    }

    config = configStorage.load();

    callQueueStorage = std::make_unique<CallQueueStorage>(QUEUE_DIR);
    const size_t purged = callQueueStorage->purgeInvalidEntries();
    if (purged > 0) {
        Serial.printf("AppController: %u arquivo(s) invalido(s) removido(s) da fila\n",
                      static_cast<unsigned>(purged));
    }
    pendingNotifications = callQueueStorage->loadAllPending();
    Serial.printf("AppController: %u notificacao(oes) pendente(s) na fila\n",
                  static_cast<unsigned>(pendingNotifications.size()));

    // AP + STA: a master cria a propria rede (pagina de configuracao) e, ao mesmo
    // tempo, conecta no roteador da casa para falar com o Telegram.
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.printf("AppController: rede '%s' criada; pagina em http://%s\n",
                      AP_SSID, WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("AppController: falha ao criar a rede propria da master");
    }
    webPortal.begin();

    if (config.wifiSsid.isEmpty()) {
        Serial.println("AppController: Wi-Fi nao configurado; alertas ficam na fila");
    } else {
        connectWifi();
    }
    return true;
}

void AppController::execute() {
    webPortal.handle();
    maintainWifi();
    collectFinishedSends();
    dispatchPendingSends();
}

void AppController::connectWifi() {
    Serial.printf("AppController: conectando ao Wi-Fi '%s'\n", config.wifiSsid.c_str());
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
    lastWifiAttemptMs = millis();
}

void AppController::maintainWifi() {
    if (config.wifiSsid.isEmpty()) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!ntpStarted) {
            Serial.printf("AppController: Wi-Fi conectado (%s)\n", WiFi.localIP().toString().c_str());
            configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
            ntpStarted = true;
        }
        return;
    }

    if (millis() - lastWifiAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
        connectWifi();
    }
}

std::vector<Notification>::iterator AppController::findPending(const String& eventId) {
    for (auto it = pendingNotifications.begin(); it != pendingNotifications.end(); ++it) {
        if (it->getEventId() == eventId) return it;
    }
    return pendingNotifications.end();
}

bool AppController::enqueueAlert(const String& originMac) {
    const PeerNode* peer = config.findPeerByMac(originMac);
    if (peer == nullptr) {
        Serial.printf("AppController: alerta ignorado, MAC desconhecido %s\n", originMac.c_str());
        return false;
    }
    if (peer->getChatIds().empty()) {
        Serial.printf("AppController: alerta ignorado, %s nao tem destinatarios\n",
                      originMac.c_str());
        return false;
    }

    const time_t now = time(nullptr);
    const String displayName = peer->getAlias().isEmpty() ? peer->getMacAddress() : peer->getAlias();
    const String message = formatAlertMessage(peer->getMessage(), displayName, now, isClockValid());

    // Sem relogio sincronizado o timestamp se repete; avancar ate o id ser unico
    // para nao sobrescrever o arquivo de outro alerta pendente.
    uint32_t timestamp = static_cast<uint32_t>(now);
    String eventId = Notification::makeEventId(peer->getMacAddress(), timestamp);
    while (findPending(eventId) != pendingNotifications.end()) {
        eventId = Notification::makeEventId(peer->getMacAddress(), ++timestamp);
    }

    Notification notification(eventId, timestamp, peer->getMacAddress(), message,
                              peer->getChatIds());

    const bool saved = callQueueStorage->enqueue(notification);
    if (!saved) {
        Serial.printf("AppController: falha ao gravar %s na fila; mantido apenas em RAM\n",
                      eventId.c_str());
    }
    // Tentar enviar mesmo sem gravar e melhor do que perder o alerta.
    pendingNotifications.push_back(std::move(notification));
    return saved;
}

bool AppController::isSending(const String& eventId, const String& chatId) const {
    for (const auto& task : telegramTasks) {
        if (!task.isFree() && task.getEventId() == eventId && task.getChatId() == chatId) {
            return true;
        }
    }
    return false;
}

void AppController::markChatAsSent(const String& eventId, const String& chatId) {
    auto it = findPending(eventId);
    if (it == pendingNotifications.end()) return;

    it->markChatAsSent(chatId);
    if (it->isCompleted()) {
        if (!callQueueStorage->remove(eventId)) {
            Serial.printf("AppController: falha ao remover %s da fila\n", eventId.c_str());
        }
        pendingNotifications.erase(it);
    } else if (!callQueueStorage->updatePending(*it)) {
        // Risco aceito: apos um reboot o chat pode receber o alerta de novo.
        Serial.printf("AppController: falha ao atualizar %s na fila\n", eventId.c_str());
    }
}

void AppController::collectFinishedSends() {
    for (auto& task : telegramTasks) {
        if (!task.isDone()) continue;

        const int status = task.getHttpStatus();
        if (isSuccess(status)) {
            Serial.printf("AppController: %s enviado para o chat %s\n",
                          task.getEventId().c_str(), task.getChatId().c_str());
            markChatAsSent(task.getEventId(), task.getChatId());
            retryDelayMs = 0;
        } else if (isPermanentRejection(status)) {
            Serial.printf("AppController: Telegram recusou o chat %s (HTTP %d); chat descartado\n",
                          task.getChatId().c_str(), status);
            markChatAsSent(task.getEventId(), task.getChatId());
        } else {
            retryDelayMs = (retryDelayMs == 0) ? RETRY_INITIAL_MS
                                               : min(retryDelayMs * 2, RETRY_MAX_MS);
            lastFailureMs = millis();
            Serial.printf("AppController: falha ao enviar %s para %s (status %d); nova tentativa em %u s\n",
                          task.getEventId().c_str(), task.getChatId().c_str(), status,
                          static_cast<unsigned>(retryDelayMs / 1000));
        }
        task.release();
    }
}

void AppController::dispatchPendingSends() {
    if (pendingNotifications.empty() || config.telegramBotToken.isEmpty() ||
        WiFi.status() != WL_CONNECTED || !isClockValid()) {
        return;
    }
    if (millis() - lastFailureMs < retryDelayMs) {
        return;  // em espera apos uma falha
    }

    for (const auto& notification : pendingNotifications) {
        for (const auto& chatId : notification.getPendingChatIds()) {
            if (isSending(notification.getEventId(), chatId)) continue;

            TelegramTask* freeTask = nullptr;
            for (auto& task : telegramTasks) {
                if (task.isFree()) {
                    freeTask = &task;
                    break;
                }
            }
            if (freeTask == nullptr) return;  // todas ocupadas; tentar na proxima iteracao

            if (!freeTask->startTask(config.telegramBotToken, notification.getEventId(), chatId,
                                 notification.getMessage())) {
                return;  // falta de memoria ou dados invalidos; tentar na proxima iteracao
            }
        }
    }
}
