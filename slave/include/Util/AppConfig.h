#pragma once

#include <Arduino.h>

namespace AppConfig {

// Pinout
constexpr uint8_t kPairingButtonPin = 4; // TODO: Confirm against final PCB schematic

// Timing
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kPairingSearchTimeoutMs = 60000; // overall bound for the broadcast search (mode 1)
constexpr uint32_t kPairingChannelDwellMs = 300;    // time spent broadcasting on each channel before moving on

// Radio
constexpr uint8_t kPairingMinChannel = 1;
constexpr uint8_t kPairingMaxChannel = 13;

// Storage
constexpr const char* kMasterLinkFilePath = "/master_link.json";

} // namespace AppConfig
