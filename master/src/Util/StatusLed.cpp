#include "Util/StatusLed.h"

namespace {
// The LED is very bright at full scale; these keep it readable without glaring.
constexpr uint8_t DIM_LEVEL = 16;
constexpr uint8_t BREATHING_MAX_LEVEL = 48;
constexpr uint32_t BREATHING_PERIOD_MS = 1000;  // 1 Hz
}

void StatusLed::update(State state, uint32_t nowMs) {
    switch (state) {
        case State::Connected:
            write(0, DIM_LEVEL, 0);
            break;
        case State::Connecting:
            write(0, 0, breathingLevel(nowMs, BREATHING_PERIOD_MS, BREATHING_MAX_LEVEL));
            break;
        case State::Disconnected:
            write(DIM_LEVEL, 0, 0);
            break;
    }
}

void StatusLed::write(uint8_t red, uint8_t green, uint8_t blue) {
    if (written && red == lastRed && green == lastGreen && blue == lastBlue) return;
    neopixelWrite(pin, red, green, blue);
    written = true;
    lastRed = red;
    lastGreen = green;
    lastBlue = blue;
}
