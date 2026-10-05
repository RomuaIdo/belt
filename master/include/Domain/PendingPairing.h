#pragma once

#include <Arduino.h>

// A belt that broadcast a PairRequest (pairing mode 1) and is waiting for
// the caregiver to finish its setup from the dashboard ("Set up" button).
// Purely in-memory: lost on reboot, same as any other in-flight ESP-NOW state.
struct PendingPairing {
    String mac;
    uint32_t receivedAtEpoch = 0; // time(nullptr) when the PairRequest arrived
};
