#pragma once

#include <Arduino.h>

namespace AppConfig {

// Pinout
constexpr uint8_t kPairingButtonPin = 2; // Target PCB pin
constexpr uint8_t kAlertButtonPin = 7;   // Target PCB pin
// BMI270 over SPI. Routed through the GPIO matrix (not the FSPI IOMUX order),
// fine at kImuSpiHz (matrix limit is ~40 MHz).
constexpr uint8_t kImuSpiCsPin = 10;    // breakout CS/CSB
constexpr uint8_t kImuSpiMosiPin = 12;  // breakout SDA/SDI
constexpr uint8_t kImuSpiSckPin = 13;   // breakout SCL/SCK
constexpr uint8_t kImuSpiMisoPin = 11;  // breakout SDO
constexpr uint8_t kImuIntPin = 9;       // BMI270 INT1 (any-motion, latched, active high)
constexpr uint8_t kNoPin = 0xFF;        // output not wired yet: the StateHandler skips it
constexpr uint8_t kStatusLedPin = 48;   // Onboard WS2812 RGB LED (ESP32-S3 Super Mini)
constexpr uint8_t kBuzzerPin = 1;
constexpr uint8_t kVibrationPin = 6;
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

// Alert: resent with the same seq until the master's AlertAck (10 x 500 ms = 5 s)
constexpr uint32_t kAlertRetryIntervalMs = 500;
constexpr uint8_t kAlertMaxAttempts = 10;

// Power. Light sleep whenever nothing is happening; woken by the IMU (any-motion on
// INT1 when still, a timer for the next FIFO read when moving) and by both buttons.
// Set to false while debugging over USB: the S3 USB serial drops during light sleep.
constexpr bool kUseLightSleep = true;
// Stay awake while a computer is on the USB port, so the serial monitor keeps working
// (the S3 USB drops during light sleep). Running on battery, the belt sleeps normally.
constexpr bool kStayAwakeOnUsb = true;

// Fall
constexpr uint32_t kFallCancelWindowMs = 5000;  // time to cancel a detected fall before the alert goes out

// Battery: LiPo 1S 3.7 V / 500 mAh (4.2 V full) through a 1:2 divider on GPIO8
// (ADC1_CH7, usable with Wi-Fi on). At most 2.1 V on the pin, inside the 11 dB range.
constexpr uint8_t kBatteryAdcPin = 8;
constexpr uint8_t kBatteryDividerRatio = 2;
constexpr uint32_t kBatteryLowMv = 3500;      // below this the battery is low
constexpr uint32_t kBatteryRecoverMv = 3650;  // hysteresis: back to normal only above this
constexpr uint32_t kBatteryReadIntervalMs = 30UL * 60UL * 1000UL;  // periodic reading (wakes from light sleep)
constexpr uint8_t kBatteryAdcSamples = 16;    // averaged to filter the ADC noise

// Feedback
constexpr unsigned int kBuzzerFreqHz = 2700;

// Radio
constexpr uint8_t kPairingMinChannel = 1;
constexpr uint8_t kPairingMaxChannel = 13;

// Storage
constexpr const char* kMasterLinkFilePath = "/master_link.json";

} // namespace AppConfig
