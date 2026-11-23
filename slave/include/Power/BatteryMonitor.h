#pragma once

#include <Arduino.h>
#include <functional>

// Battery voltage through a resistor divider on an ADC pin.
//
// Pure logic: the caller injects the pin reading (mV after the divider) and the
// clock, so it can be tested without hardware. measure() is called every
// intervalMs (the AppController wakes from light sleep for it) and on every
// system event (button click, confirmed fall); each reading restarts the interval.
//
// Hysteresis: the battery becomes low below lowMv and only recovers above
// recoverMv, so the voltage sag under load (radio, buzzer) does not make it flip.
class BatteryMonitor {
public:
    using ReadMvFn = std::function<uint32_t()>;  // mV on the pin, after the divider
    using NowMsFn = std::function<uint32_t()>;

    BatteryMonitor(ReadMvFn readPinMv, uint8_t dividerRatio, uint32_t lowMv, uint32_t recoverMv,
                   uint32_t intervalMs, NowMsFn nowMs);

    // Reads the battery, updates the low flag and restarts the interval.
    // Returns true if the battery is low.
    bool measure();

    // True when no reading was taken yet or intervalMs passed since the last one.
    bool isDue() const;

    // Time left until the next periodic reading (0 if one is due).
    uint32_t msUntilDue() const;

    bool isLow() const { return low; }
    uint32_t lastBatteryMv() const { return batteryMv; }

    // Charge of the last reading, 0..100 (LiPo 1S discharge curve, at rest).
    uint8_t lastPercent() const { return percentFromMv(batteryMv); }

    // LiPo 1S voltage -> charge, interpolated over a discharge curve (4.2 V = 100 %,
    // 3.3 V = 0 %). Rough: the voltage also depends on the load and the temperature.
    static uint8_t percentFromMv(uint32_t mv);

private:
    ReadMvFn readPinMv;
    uint8_t dividerRatio;
    uint32_t lowMv;
    uint32_t recoverMv;
    uint32_t intervalMs;
    NowMsFn nowMs;

    bool measured = false;
    bool low = false;
    uint32_t batteryMv = 0;
    uint32_t lastReadMs = 0;
};
