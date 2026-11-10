#pragma once

#include <Arduino.h>

namespace AppConfig {

// Pinout
constexpr uint8_t kPairingButtonPin = 4; // Target PCB pin
constexpr uint8_t kAlertButtonPin = 5;   // Target PCB pin
// BMI270 over SPI. GPIO10-13 are the S3 FSPI IOMUX pins.
constexpr uint8_t kImuSpiCsPin = 10;
constexpr uint8_t kImuSpiMosiPin = 11;  // breakout SDA/SDI
constexpr uint8_t kImuSpiSckPin = 12;   // breakout SCL/SCK
constexpr uint8_t kImuSpiMisoPin = 13;  // breakout SDO
constexpr uint8_t kImuIntPin = 7;       // BMI270 INT1 (any-motion, latched, active high)
constexpr uint8_t kNoPin = 0xFF;        // output not wired yet: the StateHandler skips it
constexpr uint8_t kStatusLedPin = 48;   // Onboard WS2812 RGB LED (ESP32-S3 Super Mini)
constexpr uint8_t kBuzzerPin = 6;
constexpr uint8_t kVibrationPin = 8;
// Timing
constexpr uint32_t kButtonDebounceMs = 30;
// A pairing attempt lasts this long: the caregiver has to open the page, pick the
// belt, fill the form and press Save, and the belt must still be asking meanwhile.
constexpr uint32_t kPairingTimeoutMs = 300000;
constexpr uint32_t kPairingChannelDwellMs = 300;     // Dwell time per channel while scanning
constexpr uint32_t kPairingRequestIntervalMs = 2000; // Repeat rate once a master answered
constexpr uint32_t kMasterSilenceMs = 10000;         // No PairWait for this long -> scan again

// IMU
constexpr uint32_t kImuSpiHz = 4000000;  // BMI270 allows 10 MHz; 4 MHz is safe on jumper wires
// While moving, the FIFO (~3.4 s at 50 Hz) is read this often, so no frame is lost.
constexpr uint32_t kImuReadIntervalMs = 1000;
// Must exceed the 2 s window: after a fall the person lies still, and the windows
// ending up to 2 s after the impact still have to be checked.
constexpr uint32_t kImuIdleAfterStillMs = 5000;

// Fall
constexpr uint32_t kFallCancelWindowMs = 10000;  // time to cancel a detected fall before the alert goes out

// Feedback
constexpr unsigned int kBuzzerFreqHz = 2700;

// Radio
constexpr uint8_t kPairingMinChannel = 1;
constexpr uint8_t kPairingMaxChannel = 13;

// Storage
constexpr const char* kMasterLinkFilePath = "/master_link.json";

} // namespace AppConfig
