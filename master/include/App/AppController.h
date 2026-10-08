#pragma once

#include <memory>
#include <vector>

#include "Domain/Notification.h"
#include "Domain/PendingPairing.h"
#include "Domain/PendingPairingList.h"
#include "Domain/SystemConfig.h"
#include "Messaging/TelegramTask.h"
#include "Network/EspNowTransceiver.h"
#include "Network/PairingService.h"
#include "Protocol/Message.h"
#include "Storage/CallQueueStorage.h"
#include "Storage/ConfigStorage.h"
#include "Util/StatusLed.h"
#include "Web/WebPortal.h"

class AppController {
public:
    explicit AppController(const String& configFilePath);

    // Mounts LittleFS, loads the config and the pending-alert queue, brings up the
    // master's own Wi-Fi network (config page), starts ESP-NOW and connects to the
    // saved Wi-Fi without blocking. Returns false if LittleFS fails to mount.
    bool setup();

    // One step: serves the config page, handles received ESP-NOW frames, maintains
    // Wi-Fi and the clock, collects finished sends and starts pending ones.
    // Call from every loop(), after setup().
    void execute();

    // Registers an alert from belt `originMac`: persists it to the queue and
    // appends it to the pending list; the send happens in execute(). Returns false
    // if the MAC is unknown, has no recipients or persistence fails (the alert is
    // still sent from RAM in that case). Call from the loop() task only.
    bool enqueueAlert(const String& originMac);

private:
    static constexpr size_t MAX_PARALLEL_SENDS = 2;
    static constexpr uint8_t STATUS_LED_PIN = 48;  // Onboard RGB LED on DevKitC-1 v1.0 (v1.1 uses 38)

    // Wi-Fi: scan for the saved network, connect only when it is nearby, and
    // stop retrying once the password is rejected.
    void onWifiConfigChanged();
    void connectWifi();
    void maintainWifi();
    void scheduleWifiScan(uint32_t delayMs);
    void startWifiScan();
    void checkWifiScanResult();
    void onWifiStaDisconnected(uint8_t reason);
    StatusLed::State wifiLedState() const;

    void collectFinishedSends();
    void dispatchPendingSends();
    bool isSending(const String& eventId, const String& chatId) const;
    void markChatAsSent(const String& eventId, const String& chatId);
    std::vector<Notification>::iterator findPending(const String& eventId);

    // ESP-NOW frames. The radio callback only queues them; they are handled here,
    // oldest first, on the loop() task.
    void processRadio();
    void processFrame(const RadioFrame& frame);
    void handlePairRequest(const String& mac);
    void handlePairConfirm(const String& mac, uint16_t seq);
    void handleAlert(const String& mac, uint16_t seq);
    bool isNewAlert(const String& mac, uint16_t seq);
    void sendMessage(const String& mac, Protocol::MessageType type, uint16_t seq);
    void sendPairAccept(const String& mac, uint16_t seq);

    // Called by the config page's Save: pairs a new belt (handshake) or just
    // updates a registered one.
    SaveDeviceResult saveDevice(const PeerNode& peer);
    bool pairWithBelt(const String& mac);

    std::vector<PendingPairing> getPendingPairings() { return pendingPairings.snapshot(millis()); }
    void discardPendingPairing(const String& mac) { pendingPairings.remove(mac); }

    SystemConfig config;
    ConfigStorage configStorage;
    EspNowTransceiver espNow;
    PairingService pairingService;  // handshake with the belt being saved
    PendingPairingList pendingPairings;

    // Declared after config and configStorage: it keeps references to them.
    WebPortal webPortal;

    // Created in setup(), once LittleFS is mounted.
    std::unique_ptr<CallQueueStorage> callQueueStorage;

    // RAM mirror of the queue persisted on flash.
    std::vector<Notification> pendingNotifications;

    // Fixed array: each task keeps its own address, so it must not move.
    TelegramTask telegramTasks[MAX_PARALLEL_SENDS];

    // Latest alert seq per belt, so a resent alert is acknowledged but not queued twice.
    struct LastAlert {
        String mac;
        uint16_t seq;
    };
    std::vector<LastAlert> lastAlerts;

    uint16_t nextSeq = 1;
    uint16_t acceptSeq = 0;      // seq of the PairAccept awaiting its PairConfirm
    bool pairConfirmed = false;

    // Backoff after a failed send. 0 = none; doubles per failure up to the cap.
    uint32_t lastFailureMs = 0;
    uint32_t retryDelayMs = 0;

    // Wi-Fi reconnection. autoReconnect is off, so maintainWifi() is the only
    // caller of WiFi.begin().
    uint32_t lastWifiScanMs = 0;
    uint32_t wifiScanDelayMs = 0;        // Wait after lastWifiScanMs before the next scan
    uint32_t wifiScanStartedMs = 0;
    bool wifiScanRunning = false;
    bool wifiConnecting = false;
    bool wifiWrongPassword = false;      // No retries until the Wi-Fi settings change
    uint8_t wifiQuickRetries = 0;        // Non-password failures since the last scan hit
    uint32_t wifiRetryAtMs = 0;          // 0 = no quick retry pending
    bool wifiWaitingForNetwork = false;  // Only the periodic scan is left (LED shows disconnected)
    bool ntpStarted = false;

    StatusLed statusLed{STATUS_LED_PIN};
};
