#include "App/AppController.h"
#include "Util/AppConfig.h"
#include "Protocol/PairingMessage.h"
#include <LittleFS.h>

AppController::AppController()
    : masterLinkStorage(AppConfig::kMasterLinkFilePath),
      pairingButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      pairingService(AppConfig::kPairingListenWindowMs, []() { return millis(); }) {}

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
        Serial.println("AppController: pairing button pressed, listening for master...");
        pairingService.startListening();
    }
    pairingService.tick();
}

void AppController::onEspNowMessage(const String& senderMac, const uint8_t* payload, int len) {
    if (!Protocol::isPairingMessage(payload, len)) return; // no other inbound traffic in this task

    const auto* msg = reinterpret_cast<const Protocol::PairingMessage*>(payload);
    if (msg->type != Protocol::MessageType::PairRequest) return;
    if (!pairingService.isListening()) return; // passive: ignore outside an open window

    espNow.registerPeer(senderMac);
    masterLink = MasterLink(senderMac);
    masterLinkStorage.save(masterLink);

    Protocol::PairingMessage response{};
    response.type = Protocol::MessageType::PairResponse;
    espNow.send(senderMac, reinterpret_cast<uint8_t*>(&response), sizeof(response));

    pairingService.onPairRequestReceived();
    Serial.printf("AppController: paired with master %s\n", senderMac.c_str());
}
