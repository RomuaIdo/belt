#pragma once

#include <Arduino.h>

#include "Domain/MasterLink.h"
#include "Storage/MasterLinkStorage.h"
#include "Network/EspNowTransceiver.h"
#include "Pairing/PairingService.h"
#include "Input/Button.h"

// Slave system orchestrator: owns every subsystem and drives the firmware
// lifecycle (setup/loop). Mirrors master/'s AppController pattern.
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

    void onEspNowMessage(const String& senderMac, const uint8_t* payload, int len);
};
