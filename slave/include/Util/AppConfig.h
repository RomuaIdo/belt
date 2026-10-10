#pragma once

#include <Arduino.h>

namespace AppConfig {

// Pinout
constexpr uint8_t kPairingButtonPin = 4; // Target PCB pin
constexpr uint8_t kAlertButtonPin = 5;   // Target PCB pin
constexpr uint8_t kImuSdaPin = 8;    
constexpr uint8_t kImuSclPin = 9;
constexpr uint8_t kImuIntPin = 0;       // ver
constexpr uint8_t kNoPin = 0xFF;        // output not wired yet: the StateHandler skips it
constexpr uint8_t kStatusLedPin = 48;   // Onboard WS2812 RGB LED (ESP32-S3 Super Mini)
constexpr uint8_t kBuzzerPin = 6;
constexpr uint8_t kVibrationPin = 7;
// Timing
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kAlertHoldMs = 2000;              // Hold the alert button this long to send
// A pairing attempt lasts this long: the caregiver has to open the page, pick the
// belt, fill the form and press Save, and the belt must still be asking meanwhile.
constexpr uint32_t kPairingTimeoutMs = 300000;
constexpr uint32_t kPairingChannelDwellMs = 300;     // Dwell time per channel while scanning
constexpr uint32_t kPairingRequestIntervalMs = 2000; // Repeat rate once a master answered
constexpr uint32_t kMasterSilenceMs = 10000;         // No PairWait for this long -> scan again

// IMU
constexpr uint32_t kImuI2cHz = 400000;
constexpr uint32_t kImuSampleIntervalMs = 10;  // 100 Hz, matches the sensor's SMPLRT_DIV
constexpr size_t kImuBufferSamples = 300;      // 3 s of history (~4.8 KB of RAM)
constexpr uint32_t kImuDebugPrintMs = 100;

// Feedback
constexpr unsigned int kBuzzerFreqHz = 2700;

// Radio
constexpr uint8_t kPairingMinChannel = 1;
constexpr uint8_t kPairingMaxChannel = 13;

// Storage
constexpr const char* kMasterLinkFilePath = "/master_link.json";

} // namespace AppConfig
