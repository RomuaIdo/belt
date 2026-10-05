#pragma once

#include <Arduino.h>

namespace AppConfig {

// Pinout
constexpr uint8_t kPairingButtonPin = 4; // Target PCB pin

// Timing
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kPairingSearchTimeoutMs = 60000; // Search timeout (mode 1)
constexpr uint32_t kPairingChannelDwellMs = 300;    // Dwell time per channel

// Radio
constexpr uint8_t kPairingMinChannel = 1;
constexpr uint8_t kPairingMaxChannel = 13;

// Storage
constexpr const char* kMasterLinkFilePath = "/master_link.json";

} // namespace AppConfig
