#pragma once

#include <Arduino.h>

#include "Domain/MasterLink.h"
#include "Storage/MasterLinkStorage.h"
#include "Network/EspNowTransceiver.h"
#include "Pairing/PairingService.h"
#include "Pairing/ChannelScanner.h"
#include "Input/Button.h"

// Orchestrates slave subsystems and firmware lifecycle (setup/loop).
class AppController {
public:
    AppController();

    void setup();
    void loop();

private:
    MasterLinkStorage masterLinkStorage;
    MasterLink masterLink;
    EspNowTransceiver espNow;
    Button pairingButton;
    PairingService pairingService;
    ChannelScanner channelScanner;

    // Mode 1: broadcast search across channels.
    void startBroadcastSearch();
    void sendPairingBroadcast();

    void onEspNowMessage(const String& senderMac, const uint8_t* payload, int len);
};
