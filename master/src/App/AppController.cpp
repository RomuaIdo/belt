#include "App/AppController.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <algorithm>
#include <time.h>

#include "Messaging/alert_message.h"
#include "Protocol/PairingMessage.h"
#include "Util/Clock.h"
#include "Util/MacUtils.h"

namespace {
constexpr const char* QUEUE_DIR = "/queue";

// Master's own always-on network: the config page lives at http://192.168.4.1.
constexpr const char* AP_SSID = "CintoAlerta-Master";
constexpr const char* AP_PASSWORD = "cintoalerta";  // WPA2 requires 8+ characters

constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t RETRY_INITIAL_MS = 5000;
constexpr uint32_t RETRY_MAX_MS = 300000;

// Curitiba timezone (UTC-3, no DST) and NTP servers.
constexpr const char* TIMEZONE = "<-03>3";
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.google.com";

// Bounded wait for a dashboard-initiated (mode 2) PairResponse.
constexpr uint32_t PAIRING_CONFIRM_TIMEOUT_MS = 10000;
constexpr uint8_t ESP_NOW_ACK_STATUS_OK = 0x01;
constexpr uint8_t ALERT_EVENT_QUEUE_DEPTH = 8;

bool isSuccess(int httpStatus) { return httpStatus >= 200 && httpStatus < 300; }

// Telegram permanently rejected the chat (no such chat, bot blocked):
// retrying won't help.
bool isPermanentRejection(int httpStatus) { return httpStatus == 400 || httpStatus == 403; }

Protocol::PairingMessage makePairingMessage(Protocol::MessageType type) {
    Protocol::PairingMessage msg{};
    msg.type = type;
    Protocol::fillKey(msg);
    return msg;
}

// Fixed-size POD: the only thing safe to memcpy across the FreeRTOS queue.
struct PendingAlertEvent {
    char originMac[18];
};
}

AppController::AppController(const String& configFilePath)
    : configStorage(configFilePath),
      pairingService(PAIRING_CONFIRM_TIMEOUT_MS, []() { return millis(); }),
      webPortal(config, configStorage, [this] {
          // The page saved a different Wi-Fi: drop the current network and connect to the new one.
          WiFi.disconnect();
          connectWifi();
      }) {}

AppController::~AppController() {
    if (alertEventQueue != nullptr) {
        vQueueDelete(alertEventQueue);
    }
}

bool AppController::setup() {
    if (!LittleFS.begin(false)) {
        return false;
    }

    config = configStorage.load();

    callQueueStorage = std::make_unique<CallQueueStorage>(QUEUE_DIR);
    const size_t purged = callQueueStorage->purgeInvalidEntries();
    if (purged > 0) {
        Serial.printf("AppController: purged %u invalid queue file(s)\n", static_cast<unsigned>(purged));
    }
    pendingNotifications = callQueueStorage->loadAllPending();
    Serial.printf("AppController: %u pending notification(s) in the queue\n",
                  static_cast<unsigned>(pendingNotifications.size()));

    alertEventQueue = xQueueCreate(ALERT_EVENT_QUEUE_DEPTH, sizeof(PendingAlertEvent));

    // AP + STA: the master hosts its own network (config page) while also
    // connecting to the home router to reach Telegram.
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.printf("AppController: network '%s' up; page at http://%s\n",
                      AP_SSID, WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("AppController: failed to start the master's own network");
    }

    if (!espNow.init()) {
        Serial.println("AppController: ESP-NOW init failed");
    } else {
        registerAllPeers();
        espNow.setOnMessageReceived([this](const String& mac, const uint8_t* data, int len) {
            onEspNowMessage(mac, data, len);
        });
    }

    webPortal.setListPendingPairings([this] { return getPendingPairings(); });
    webPortal.setOnDiscardPending([this](const String& mac) { discardPendingPairing(mac); });
    webPortal.setOnSaveDevice([this](const PeerNode& peer) { return pairAndSaveNewBelt(peer); });
    webPortal.begin();

    if (config.wifiSsid.isEmpty()) {
        Serial.println("AppController: Wi-Fi not configured; alerts stay queued");
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
    pairingService.tick();

    PendingAlertEvent evt;
    while (alertEventQueue != nullptr && xQueueReceive(alertEventQueue, &evt, 0) == pdTRUE) {
        enqueueAlert(String(evt.originMac));
    }
}

void AppController::connectWifi() {
    Serial.printf("AppController: connecting to Wi-Fi '%s'\n", config.wifiSsid.c_str());
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
    lastWifiAttemptMs = millis();
}

void AppController::maintainWifi() {
    if (config.wifiSsid.isEmpty()) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!ntpStarted) {
            Serial.printf("AppController: Wi-Fi connected (%s)\n", WiFi.localIP().toString().c_str());
            configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
            ntpStarted = true;
        }
        return;
    }

    if (millis() - lastWifiAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
        connectWifi();
    }
}

void AppController::registerAllPeers() {
    for (const auto& peer : config.getPeers()) {
        espNow.registerPeer(peer.getMacAddress());
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
        Serial.printf("AppController: alert ignored, unknown MAC %s\n", originMac.c_str());
        return false;
    }
    if (peer->getChatIds().empty()) {
        Serial.printf("AppController: alert ignored, %s has no recipients\n", originMac.c_str());
        return false;
    }

    const time_t now = time(nullptr);
    const String displayName = peer->getAlias().isEmpty() ? peer->getMacAddress() : peer->getAlias();
    const String message = formatAlertMessage(peer->getMessage(), displayName, now, isClockValid());

    // Without a synced clock the timestamp repeats; advance it until the id
    // is unique so this doesn't overwrite another pending alert's file.
    uint32_t timestamp = static_cast<uint32_t>(now);
    String eventId = Notification::makeEventId(peer->getMacAddress(), timestamp);
    while (findPending(eventId) != pendingNotifications.end()) {
        eventId = Notification::makeEventId(peer->getMacAddress(), ++timestamp);
    }

    Notification notification(eventId, timestamp, peer->getMacAddress(), message,
                              peer->getChatIds());

    const bool saved = callQueueStorage->enqueue(notification);
    if (!saved) {
        Serial.printf("AppController: failed to persist %s; keeping it in RAM only\n", eventId.c_str());
    }
    // Trying to send even without a successful write beats losing the alert.
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
            Serial.printf("AppController: failed to remove %s from the queue\n", eventId.c_str());
        }
        pendingNotifications.erase(it);
    } else if (!callQueueStorage->updatePending(*it)) {
        // Accepted risk: a chat may receive the alert again after a reboot.
        Serial.printf("AppController: failed to update %s in the queue\n", eventId.c_str());
    }
}

void AppController::collectFinishedSends() {
    for (auto& task : telegramTasks) {
        if (!task.isDone()) continue;

        const int status = task.getHttpStatus();
        if (isSuccess(status)) {
            Serial.printf("AppController: %s sent to chat %s\n",
                          task.getEventId().c_str(), task.getChatId().c_str());
            markChatAsSent(task.getEventId(), task.getChatId());
            retryDelayMs = 0;
        } else if (isPermanentRejection(status)) {
            Serial.printf("AppController: Telegram rejected chat %s (HTTP %d); dropping it\n",
                          task.getChatId().c_str(), status);
            markChatAsSent(task.getEventId(), task.getChatId());
        } else {
            retryDelayMs = (retryDelayMs == 0) ? RETRY_INITIAL_MS
                                               : min(retryDelayMs * 2, RETRY_MAX_MS);
            lastFailureMs = millis();
            Serial.printf("AppController: failed to send %s to %s (status %d); retrying in %u s\n",
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
        return;  // backing off after a failure
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
            if (freeTask == nullptr) return;  // all busy; retry next iteration

            if (!freeTask->startTask(config.telegramBotToken, notification.getEventId(), chatId,
                                 notification.getMessage())) {
                return;  // out of memory or invalid input; retry next iteration
            }
        }
    }
}

void AppController::onEspNowMessage(const String& senderMac, const uint8_t* payload, int len) {
    // Tagged pairing traffic takes its own path; the alert flow below is untouched.
    if (Protocol::isPairingMessage(payload, len)) {
        handlePairingMessage(senderMac, payload, len);
        return;
    }

    // ACKing immediately, before any lookup or HTTP work, stops the belt's
    // retry loop as fast as possible.
    espNow.sendAck(senderMac, ESP_NOW_ACK_STATUS_OK);

    if (config.findPeerByMac(senderMac) == nullptr) {
        Serial.printf("AppController: alert from unregistered MAC %s ignored\n", senderMac.c_str());
        return;
    }

    // Deferred to execute() so the ESP-NOW/Wi-Fi task never blocks on it.
    PendingAlertEvent evt{};
    senderMac.toCharArray(evt.originMac, sizeof(evt.originMac));
    if (alertEventQueue != nullptr) {
        xQueueSend(alertEventQueue, &evt, 0);
    }
}

void AppController::handlePairingMessage(const String& senderMac, const uint8_t* payload, int len) {
    (void)len; // already validated by isPairingMessage()'s length check
    const auto* msg = reinterpret_cast<const Protocol::PairingMessage*>(payload);

    if (msg->type == Protocol::MessageType::PairRequest) {
        // Mode 1: a belt is broadcasting unprompted. Always reply (the shared
        // key is the gate), so it stops searching; it only becomes a real
        // peer once the caregiver finishes setup from the dashboard.
        espNow.registerPeer(senderMac);
        Protocol::PairingMessage response = makePairingMessage(Protocol::MessageType::PairResponse);
        espNow.send(senderMac, reinterpret_cast<uint8_t*>(&response), sizeof(response));

        if (config.findPeerByMac(senderMac) == nullptr) {
            addPendingPairing(senderMac);
        }
        Serial.printf("AppController: belt %s is waiting to be set up\n", senderMac.c_str());
        return;
    }

    if (msg->type == Protocol::MessageType::PairResponse) {
        // Mode 2: confirms the belt the caregiver entered on the dashboard.
        if (!pairingService.isPendingFor(senderMac)) return; // stray/expired/wrong MAC
        espNow.registerPeer(senderMac);
        config.addPeer(stagedPeer);
        if (!configStorage.save(config)) {
            Serial.println("AppController: failed to persist configuration");
        }
        pairingService.confirm();
        Serial.printf("AppController: paired new belt %s (dashboard)\n", senderMac.c_str());
    }
}

void AppController::addPendingPairing(const String& mac) {
    for (auto& pending : pendingPairings) {
        if (MacUtils::equal(pending.mac, mac)) {
            pending.receivedAtEpoch = static_cast<uint32_t>(time(nullptr));
            return;
        }
    }
    pendingPairings.push_back(PendingPairing{mac, static_cast<uint32_t>(time(nullptr))});
}

void AppController::discardPendingPairing(const String& mac) {
    pendingPairings.erase(
        std::remove_if(pendingPairings.begin(), pendingPairings.end(),
                       [&](const PendingPairing& pending) { return MacUtils::equal(pending.mac, mac); }),
        pendingPairings.end());
}

bool AppController::pairAndSaveNewBelt(const PeerNode& peer) {
    const String& mac = peer.getMacAddress();

    if (config.findPeerByMac(mac) != nullptr) {
        // Editing an already-registered belt: nothing to pair.
        config.removePeer(mac);
        config.addPeer(peer);
        return configStorage.save(config);
    }

    const bool alreadyPending = std::any_of(
        pendingPairings.begin(), pendingPairings.end(),
        [&](const PendingPairing& pending) { return MacUtils::equal(pending.mac, mac); });
    if (alreadyPending) {
        // The belt already broadcast and got its PairResponse (mode 1);
        // finishing the dashboard form just finalizes it.
        discardPendingPairing(mac);
        config.addPeer(peer);
        return configStorage.save(config);
    }

    // Genuinely new MAC: dashboard-initiated pairing (mode 2). Unicast a
    // PairRequest and block, bounded, for the belt's PairResponse — the
    // ESP-NOW RX callback still fires while this loop blocks execute().
    stagedPeer = peer;
    pairingService.start(mac);
    Protocol::PairingMessage request = makePairingMessage(Protocol::MessageType::PairRequest);
    espNow.send(mac, reinterpret_cast<uint8_t*>(&request), sizeof(request));

    const uint32_t startMs = millis();
    while (pairingService.isActive() && (millis() - startMs) < PAIRING_CONFIRM_TIMEOUT_MS) {
        delay(50);
        pairingService.tick();
    }

    return config.findPeerByMac(mac) != nullptr;
}
