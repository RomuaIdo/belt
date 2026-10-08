#include "App/AppController.h"

#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include "Messaging/alert_message.h"
#include "Network/WifiPolicy.h"
#include "Util/Clock.h"
#include "Util/MacUtils.h"

namespace {
constexpr const char* QUEUE_DIR = "/queue";

// Master AP network credentials (http://192.168.4.1).
constexpr const char* AP_SSID = "CintoAlerta-Master";
constexpr const char* AP_PASSWORD = "cintoalerta";  // Minimum 8 characters for WPA2

// Name on the home network: http://cintoalerta.local (mDNS) and the router's client list.
constexpr const char* HOSTNAME = "cintoalerta";

// How often to re-scan for saved network when disconnected.
constexpr uint32_t WIFI_SCAN_INTERVAL_MS = 30000;
constexpr uint32_t WIFI_SCAN_RETRY_MS = 1000;  // After a scan that failed to start or run

// A handful of quick retries absorb flaky-but-not-wrong-password disconnects (e.g. a
// router answering AUTH_EXPIRE a few times before the handshake goes through) without
// waiting out a full re-scan cycle. autoReconnect is off, so this is the only retry.
constexpr uint8_t WIFI_QUICK_RETRY_LIMIT = 5;
constexpr uint32_t WIFI_QUICK_RETRY_DELAY_MS = 1000;

constexpr uint32_t RETRY_INITIAL_MS = 5000;
constexpr uint32_t RETRY_MAX_MS = 300000;

// Timezone (UTC-3, no DST) and NTP servers.
constexpr const char* TIMEZONE = "<-03>3";
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.google.com";

// Pairing confirmation timeout and resend interval.
constexpr uint32_t PAIRING_CONFIRM_TIMEOUT_MS = 1500;
constexpr uint32_t PAIR_ACCEPT_RESEND_MS = 500;

// A belt asking to pair repeats its request every couple of seconds; one that stays
// silent this long is no longer trying and leaves the "waiting" list.
constexpr uint32_t PENDING_PAIRING_TTL_MS = 15000;

bool isSuccess(int httpStatus) { return httpStatus >= 200 && httpStatus < 300; }

// Permanent Telegram rejection (invalid chat or bot blocked).
bool isPermanentRejection(int httpStatus) { return httpStatus == 400 || httpStatus == 403; }
}

AppController::AppController(const String& configFilePath)
    : configStorage(configFilePath),
      pairingService(PAIRING_CONFIRM_TIMEOUT_MS, []() { return millis(); }),
      pendingPairings(PENDING_PAIRING_TTL_MS),
      webPortal(config, configStorage, [this] { onWifiConfigChanged(); }) {}

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

    // AP + STA: host config portal while maintaining Wi-Fi uplink.
    WiFi.setHostname(HOSTNAME);  // Must precede WiFi.mode() to apply to the STA interface
    WiFi.mode(WIFI_AP_STA);
    // Off on purpose: the IDF's own autoReconnect retries forever on several
    // disconnect reasons (including the ones a wrong WPA2 password actually
    // produces, e.g. 4-way handshake timeout), bypassing wifiWrongPassword below.
    // maintainWifi() is the only thing that calls WiFi.begin() again.
    WiFi.setAutoReconnect(false);
    if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.printf("AppController: network '%s' up; page at http://%s\n",
                      AP_SSID, WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("AppController: failed to start the master's own network");
    }

    // Announces the page on the home network once the STA gets an IP.
    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("AppController: mDNS up; page at http://%s.local\n", HOSTNAME);
    } else {
        Serial.println("AppController: mDNS failed; use the IP address instead");
    }

    if (espNow.init()) {
        Serial.printf("AppController: ESP-NOW up; master MAC %s\n", WiFi.macAddress().c_str());
    } else {
        Serial.println("AppController: ESP-NOW init failed; pairing and alerts unavailable");
    }

    webPortal.setListPendingPairings([this] { return getPendingPairings(); });
    webPortal.setOnDiscardPending([this](const String& mac) { discardPendingPairing(mac); });
    webPortal.setOnSaveDevice([this](const PeerNode& peer) { return saveDevice(peer); });
    webPortal.setWifiWrongPasswordProvider([this] { return wifiWrongPassword; });
    webPortal.begin();

    // Detect authentication failures to avoid repeated retries with invalid credentials.
    WiFi.onEvent(
        [this](arduino_event_id_t /*event*/, arduino_event_info_t info) {
            onWifiStaDisconnected(info.wifi_sta_disconnected.reason);
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    if (config.wifiSsid.isEmpty()) {
        Serial.println("AppController: Wi-Fi not configured; alerts stay queued");
    }
    return true;
}

void AppController::execute() {
    const uint32_t handleStartMs = millis();
    webPortal.handle();
    const uint32_t handleMs = millis() - handleStartMs;
    if (handleMs > 1000) {
        Serial.printf("AppController: web request held the loop for %u ms\n", static_cast<unsigned>(handleMs));
    }
    processRadio();
    maintainWifi();
    statusLed.update(wifiLedState(), millis());
    collectFinishedSends();
    dispatchPendingSends();
}

StatusLed::State AppController::wifiLedState() const {
    if (WiFi.status() == WL_CONNECTED) return StatusLed::State::Connected;
    if (config.wifiSsid.isEmpty() || wifiWrongPassword || wifiWaitingForNetwork) {
        return StatusLed::State::Disconnected;
    }
    return StatusLed::State::Connecting;
}

void AppController::onWifiConfigChanged() {
    // Reset connection state and trigger immediate re-scan.
    Serial.println("AppController: Wi-Fi settings changed");
    WiFi.disconnect();
    if (wifiScanRunning) WiFi.scanDelete();
    wifiScanRunning = false;
    wifiConnecting = false;
    wifiWrongPassword = false;
    wifiQuickRetries = 0;
    wifiRetryAtMs = 0;
    wifiWaitingForNetwork = false;
    ntpStarted = false;
    scheduleWifiScan(0);  // Scan right away instead of waiting out the interval.
}

void AppController::connectWifi() {
    Serial.printf("AppController: connecting to Wi-Fi '%s'\n", config.wifiSsid.c_str());
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
    wifiConnecting = true;
    wifiWaitingForNetwork = false;
}

void AppController::scheduleWifiScan(uint32_t delayMs) {
    lastWifiScanMs = millis();
    wifiScanDelayMs = delayMs;
}

void AppController::startWifiScan() {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        Serial.println("AppController: Wi-Fi scan skipped, another scan is still running");
        scheduleWifiScan(WIFI_SCAN_RETRY_MS);
        return;
    }

    // The IDF refuses to start a scan while the STA is still connecting or
    // disconnecting (e.g. right after Save's WiFi.disconnect()). Retry shortly
    // instead of waiting out the full interval.
    if (WiFi.scanNetworks(true /* async */) == WIFI_SCAN_FAILED) {
        Serial.println("AppController: Wi-Fi scan could not start; retrying shortly");
        scheduleWifiScan(WIFI_SCAN_RETRY_MS);
        return;
    }
    Serial.println("AppController: Wi-Fi scan started");
    wifiScanStartedMs = millis();
    wifiScanRunning = true;
    scheduleWifiScan(WIFI_SCAN_INTERVAL_MS);
}

void AppController::checkWifiScanResult() {
    const int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;

    wifiScanRunning = false;
    const unsigned scanMs = static_cast<unsigned>(millis() - wifiScanStartedMs);
    if (result < 0) {
        Serial.printf("AppController: Wi-Fi scan failed after %u ms; retrying shortly\n", scanMs);
        scheduleWifiScan(WIFI_SCAN_RETRY_MS);
        return;
    }
    Serial.printf("AppController: Wi-Fi scan done in %u ms, %d network(s)\n", scanMs, result);
    if (result == 0) {
        wifiWaitingForNetwork = true;
        return;
    }

    bool nearby = false;
    for (int i = 0; i < result; ++i) {
        if (WiFi.SSID(i) == config.wifiSsid) {
            nearby = true;
            break;
        }
    }
    WiFi.scanDelete();

    if (nearby) {
        wifiQuickRetries = 0;
        connectWifi();
    } else {
        wifiWaitingForNetwork = true;
        Serial.printf("AppController: saved network '%s' not seen nearby; not connecting\n",
                      config.wifiSsid.c_str());
    }
}

void AppController::onWifiStaDisconnected(uint8_t reason) {
    if (!wifiConnecting) return;  // Not our attempt failing (e.g. a drop while idle).
    wifiConnecting = false;

    if (isWifiAuthFailure(reason)) {
        wifiWrongPassword = true;
        wifiRetryAtMs = 0;
        // A wrong password still leaves the station associated at the 802.11 level
        // (only the 4-way handshake failed), and some routers keep re-offering the
        // handshake to an associated client: without this, the retries kept coming
        // from the driver itself every few seconds, with nothing left on our side
        // to stop (confirmed on hardware). Disconnecting tears the association down.
        WiFi.disconnect();
        Serial.printf("AppController: Wi-Fi '%s' rejected the password (reason %u); giving up\n",
                      config.wifiSsid.c_str(), static_cast<unsigned>(reason));
        return;
    }

    // Transient (router busy, brief timeout, ...): a few quick retries before
    // falling back to the slower scan-and-check cycle.
    if (++wifiQuickRetries <= WIFI_QUICK_RETRY_LIMIT) {
        wifiRetryAtMs = millis() + WIFI_QUICK_RETRY_DELAY_MS;
    } else {
        wifiWaitingForNetwork = true;
        Serial.printf("AppController: Wi-Fi '%s' still not connecting (reason %u) after %u quick retries\n",
                      config.wifiSsid.c_str(), static_cast<unsigned>(reason),
                      static_cast<unsigned>(WIFI_QUICK_RETRY_LIMIT));
    }
}

void AppController::maintainWifi() {
    if (config.wifiSsid.isEmpty()) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnecting = false;
        wifiWrongPassword = false;
        wifiQuickRetries = 0;
        wifiRetryAtMs = 0;
        wifiWaitingForNetwork = false;
        if (!ntpStarted) {
            Serial.printf("AppController: Wi-Fi connected (%s)\n", WiFi.localIP().toString().c_str());
            configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
            ntpStarted = true;
        }
        return;
    }

    // Stop automatic retries on wrong password until credentials change.
    if (wifiWrongPassword) {
        return;
    }

    if (wifiConnecting) {
        return;  // Awaiting WL_CONNECTED or the disconnected event above.
    }

    if (wifiRetryAtMs != 0) {
        if (millis() >= wifiRetryAtMs) {
            wifiRetryAtMs = 0;
            connectWifi();
        }
        return;
    }

    if (wifiScanRunning) {
        checkWifiScanResult();
        return;
    }

    if (millis() - lastWifiScanMs >= wifiScanDelayMs) {
        startWifiScan();
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

    // Ensure unique event ID even without synced clock.
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
    // Queue in memory even if flash persistence fails.
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
        // Chat may receive duplicate alert on reboot if update fails.
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
        return;  // Backoff delay active
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
            if (freeTask == nullptr) return;  // All tasks busy

            if (!freeTask->startTask(config.telegramBotToken, notification.getEventId(), chatId,
                                 notification.getMessage())) {
                return;  // Task creation failed
            }
        }
    }
}

void AppController::processRadio() {
    RadioFrame frame;
    while (espNow.receive(frame)) {
        processFrame(frame);
    }
}

void AppController::processFrame(const RadioFrame& frame) {
    // Anything without our magic, a known type and the exact size is not ours (other
    // ESP-NOW traffic) or is malformed: dropped silently.
    Protocol::Header header;
    if (!Protocol::parseFrame(frame.data, frame.len, header)) return;

    const String mac = MacUtils::format(frame.mac);
    switch (header.type) {
        case Protocol::MessageType::PairRequest:
            handlePairRequest(mac);
            break;
        case Protocol::MessageType::PairConfirm:
            handlePairConfirm(mac, header.seq);
            break;
        case Protocol::MessageType::Alert:
            handleAlert(mac, header.seq);
            break;
        default:
            // PairWait, PairAccept and AlertAck are sent by the master, never to it.
            Serial.printf("AppController: unexpected message type 0x%02X from %s dropped\n",
                          static_cast<unsigned>(header.type), mac.c_str());
            break;
    }
}

void AppController::sendMessage(const String& mac, Protocol::MessageType type, uint16_t seq) {
    uint8_t frame[Protocol::kMaxFrameSize];
    const size_t size = Protocol::buildFrame(frame, sizeof(frame), type, seq);
    espNow.send(mac, frame, size);
}

void AppController::sendPairAccept(const String& mac, uint16_t seq) {
    uint8_t frame[Protocol::kMaxFrameSize];
    const size_t size = Protocol::buildPairAccept(frame, sizeof(frame), seq, EspNowTransceiver::channel());
    espNow.send(mac, frame, size);
}

void AppController::handlePairRequest(const String& mac) {
    if (config.findPeerByMac(mac) != nullptr) {
        // Registered belt searching again (lost the channel or its memory): accept it
        // right away. It answers with a PairConfirm, which needs no handling here.
        sendPairAccept(mac, nextSeq++);
        return;
    }

    // New belt: list it for the caregiver and tell it to wait. The belt keeps
    // repeating its request, which keeps the entry alive.
    const uint32_t epoch = isClockValid() ? static_cast<uint32_t>(time(nullptr)) : 0;
    if (pendingPairings.heard(mac, millis(), epoch)) {
        Serial.printf("AppController: belt %s is waiting to be set up\n", mac.c_str());
    }
    sendMessage(mac, Protocol::MessageType::PairWait, nextSeq++);
}

void AppController::handlePairConfirm(const String& mac, uint16_t seq) {
    // Only meaningful while saveDevice() waits for it; anything else is stale.
    if (!pairingService.isPendingFor(mac) || seq != acceptSeq) return;
    pairConfirmed = true;
    pairingService.confirm();
}

void AppController::handleAlert(const String& mac, uint16_t seq) {
    if (config.findPeerByMac(mac) == nullptr) {
        // Not acknowledged: the belt must not believe an alert we dropped was delivered.
        Serial.printf("AppController: alert from unregistered MAC %s ignored\n", mac.c_str());
        return;
    }

    // Queue the alert at the back of the pending list, then acknowledge so the belt
    // stops resending. A resent alert (same seq) is acknowledged but not queued again.
    if (isNewAlert(mac, seq)) {
        enqueueAlert(mac);
    }
    sendMessage(mac, Protocol::MessageType::AlertAck, seq);
}

bool AppController::isNewAlert(const String& mac, uint16_t seq) {
    for (auto& last : lastAlerts) {
        if (MacUtils::equal(last.mac, mac)) {
            if (last.seq == seq) return false;
            last.seq = seq;
            return true;
        }
    }
    lastAlerts.push_back({mac, seq});
    return true;
}

SaveDeviceResult AppController::saveDevice(const PeerNode& peer) {
    const String& mac = peer.getMacAddress();

    if (config.findPeerByMac(mac) != nullptr) {
        // Already paired: only its name, message and recipients change.
        config.removePeer(mac);
        config.addPeer(peer);
        return configStorage.save(config) ? SaveDeviceResult::Updated : SaveDeviceResult::StorageError;
    }

    if (!pendingPairings.contains(mac, millis())) {
        return SaveDeviceResult::NotWaiting;
    }
    if (!pairWithBelt(mac)) {
        Serial.printf("AppController: belt %s did not confirm the pairing\n", mac.c_str());
        return SaveDeviceResult::NoConfirmation;
    }

    // The belt saved the master's MAC and channel; now the master saves the belt's.
    config.addPeer(peer);
    pendingPairings.remove(mac);
    if (!configStorage.save(config)) {
        return SaveDeviceResult::StorageError;
    }
    Serial.printf("AppController: paired new belt %s\n", mac.c_str());
    return SaveDeviceResult::Paired;
}

bool AppController::pairWithBelt(const String& mac) {
    acceptSeq = nextSeq++;
    pairConfirmed = false;
    pairingService.start(mac);

    uint32_t lastSendMs = 0;
    bool sentOnce = false;
    while (pairingService.isActive()) {
        const uint32_t now = millis();
        if (!sentOnce || now - lastSendMs >= PAIR_ACCEPT_RESEND_MS) {
            sendPairAccept(mac, acceptSeq);
            lastSendMs = now;
            sentOnce = true;
        }

        // Wait for frames in short slices. Anything that is not the awaited
        // PairConfirm (other belts, alerts) is handled as usual.
        RadioFrame frame;
        if (espNow.receive(frame, 20)) {
            processFrame(frame);
        }
        pairingService.tick();
    }

    pairingService.stop();
    return pairConfirmed;
}