#pragma once

#include <Arduino.h>

namespace AppConfig {

// Pinout
constexpr uint8_t kPairingButtonPin = 4; // TODO: Confirm against final PCB schematic

// Timing
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kPairingListenWindowMs = 60000;

// Storage
constexpr const char* kMasterLinkFilePath = "/master_link.json";

} // namespace AppConfig
