#pragma once

#include <Arduino.h>

// Belt waiting for dashboard setup in pairing mode 1 (in-memory only).
struct PendingPairing {
    String mac;
    uint32_t receivedAtEpoch = 0; // Timestamp when PairRequest arrived
};
