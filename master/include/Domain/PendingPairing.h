#pragma once

#include <Arduino.h>

// Belt that sent a PairRequest and is waiting for the caregiver to press Save
// (in-memory only).
struct PendingPairing {
    String mac;
    uint32_t lastHeardMs = 0;      // millis() of its latest PairRequest
    uint32_t receivedAtEpoch = 0;  // wall-clock time of that request; 0 if the clock was not synced
};
