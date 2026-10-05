#pragma once

#include <memory>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "Domain/Notification.h"
#include "Domain/PendingPairing.h"
#include "Domain/SystemConfig.h"
#include "Messaging/TelegramTask.h"
#include "Network/EspNowTransceiver.h"
#include "Network/PairingService.h"
#include "Storage/CallQueueStorage.h"
#include "Storage/ConfigStorage.h"
#include "Web/WebPortal.h"

class AppController {
public:
    explicit AppController(const String& configFilePath);
    ~AppController();

    bool setup();
    void execute();
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

    void registerAllPeers();
    void onEspNowMessage(const String& senderMac, const uint8_t* payload, int len);
    void handlePairingMessage(const String& senderMac, const uint8_t* payload, int len);

    // Pairing mode 1 (belt broadcasts, unprompted): replies immediately so
    // the belt stops searching, but only remembers the MAC here until the
    // caregiver finishes setup from the dashboard.
    void addPendingPairing(const String& mac);
    // Pairing mode 2 (dashboard-initiated): unicasts a PairRequest and blocks
    // (bounded) for the belt's PairResponse before persisting. Called from
    // WebPortal's save-device handler. Returns false on timeout.
    bool pairAndSaveNewBelt(const PeerNode& peer);
    std::vector<PendingPairing> getPendingPairings() const { return pendingPairings; }
    void discardPendingPairing(const String& mac);

    SystemConfig config;
    ConfigStorage configStorage;
    EspNowTransceiver espNow;
    PairingService pairingService;
    WebPortal webPortal;
    std::unique_ptr<CallQueueStorage> callQueueStorage;
    std::vector<Notification> pendingNotifications;
    std::vector<PendingPairing> pendingPairings;
    TelegramTask telegramTasks[MAX_PARALLEL_SENDS];
    PeerNode stagedPeer; // peer being confirmed by pairAndSaveNewBelt()

    // Bridges the ESP-NOW RX callback (radio task context) to execute()
    // (loop() task), so alert dispatch never runs from an interrupt context.
    QueueHandle_t alertEventQueue = nullptr;

    uint32_t lastFailureMs = 0;
    uint32_t retryDelayMs = 0;
    uint32_t lastWifiAttemptMs = 0;
    bool ntpStarted = false;
};
