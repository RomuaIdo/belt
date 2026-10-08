#pragma once

#include <Arduino.h>
#include <math.h>

// Brightness of a smooth "breathing" pulse: 0 at the start of each period,
// maxLevel halfway through, back to 0 at the end (raised cosine, so it eases
// in and out instead of ramping linearly).
inline uint8_t breathingLevel(uint32_t nowMs, uint32_t periodMs, uint8_t maxLevel) {
    const float phase = static_cast<float>(nowMs % periodMs) / static_cast<float>(periodMs);
    const float level = maxLevel * (1.0f - cosf(2.0f * static_cast<float>(PI) * phase)) / 2.0f;
    return static_cast<uint8_t>(lroundf(level));
}

// Board's addressable RGB LED showing the master's home Wi-Fi state.
class StatusLed {
public:
    enum class State {
        Disconnected,  // dim steady red
        Connecting,    // blue, breathing at 1 Hz
        Connected,     // dim steady green
    };

    explicit StatusLed(uint8_t pin) : pin(pin) {}

    // Call often (every loop()); writes the LED only when its color changes.
    void update(State state, uint32_t nowMs);

private:
    void write(uint8_t red, uint8_t green, uint8_t blue);

    uint8_t pin;
    bool written = false;
    uint8_t lastRed = 0;
    uint8_t lastGreen = 0;
    uint8_t lastBlue = 0;
};
