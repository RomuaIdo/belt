#include "App/AppController.h"
#include "Util/AppConfig.h"
#include "Protocol/PairingMessage.h"
#include <LittleFS.h>

namespace {

Protocol::PairingMessage makePairingMessage(Protocol::MessageType type) {
    Protocol::PairingMessage msg{};
    msg.type = type;
    Protocol::fillKey(msg);
    return msg;
}

} // namespace

AppController::AppController()
    : masterLinkStorage(AppConfig::kMasterLinkFilePath),
      pairingButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      pairingService(AppConfig::kPairingSearchTimeoutMs, []() { return millis(); }),
      channelScanner(AppConfig::kPairingMinChannel, AppConfig::kPairingMaxChannel,
                     AppConfig::kPairingChannelDwellMs, []() { return millis(); }) {}

void AppController::setup() {
    Serial.begin(115200);

    if (!LittleFS.begin(true)) {
        Serial.println("AppController: failed to mount LittleFS");
    }

    masterLink = masterLinkStorage.load();
    if (masterLink.hasMac()) {
        // Re-register so a future send() to the master works after a reboot.
        espNow.registerPeer(masterLink.getMacAddress());
        Serial.printf("AppController: restored paired master %s\n", masterLink.getMacAddress().c_str());
    }

    pinMode(AppConfig::kPairingButtonPin, INPUT_PULLUP);

    if (!espNow.init()) {
        Serial.println("AppController: ESP-NOW init failed");
    } else {
        espNow.setOnMessageReceived([this](const String& mac, const uint8_t* data, int len) {
            onEspNowMessage(mac, data, len);
        });
    }

    Serial.println("AppController: setup complete");
}

void AppController::loop() {
    bool pressed = pairingButton.update(digitalRead(AppConfig::kPairingButtonPin), millis());
    if (pressed) {
        Serial.println("AppController: pairing button pressed, broadcasting for master...");
        startBroadcastSearch();
    }

    if (pairingService.isSearching() && channelScanner.tick()) {
        espNow.setChannel(channelScanner.currentChannel());
        sendPairingBroadcast();
    }

    pairingService.tick();
}

void AppController::startBroadcastSearch() {
    pairingService.startSearching();
    channelScanner.reset();
    espNow.setChannel(channelScanner.currentChannel());
    sendPairingBroadcast();
}

void AppController::sendPairingBroadcast() {
    Protocol::PairingMessage msg = makePairingMessage(Protocol::MessageType::PairRequest);
    espNow.send(Protocol::kBroadcastMac, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
}

void AppController::onEspNowMessage(const String& senderMac, const uint8_t* payload, int len) {
    if (!Protocol::isPairingMessage(payload, len)) return;
    const auto* msg = reinterpret_cast<const Protocol::PairingMessage*>(payload);

    if (msg->type == Protocol::MessageType::PairRequest) {
        // Mode 2: master unicast directly to us — it already knows our MAC
        // from the dashboard, so no window/state check is needed here.
        espNow.registerPeer(senderMac);
        masterLink = MasterLink(senderMac);
        masterLinkStorage.save(masterLink);

        Protocol::PairingMessage response = makePairingMessage(Protocol::MessageType::PairResponse);
        espNow.send(senderMac, reinterpret_cast<uint8_t*>(&response), sizeof(response));
        Serial.printf("AppController: paired with master %s (dashboard)\n", senderMac.c_str());
        return;
    }

    if (msg->type == Protocol::MessageType::PairResponse) {
        // Mode 1: confirms the master found while broadcast-searching.
        if (!pairingService.isSearching()) return; // stray/late response
        espNow.registerPeer(senderMac);
        masterLink = MasterLink(senderMac);
        masterLinkStorage.save(masterLink);
        pairingService.onPairResponseReceived();
        Serial.printf("AppController: paired with master %s (broadcast)\n", senderMac.c_str());
    }
}
