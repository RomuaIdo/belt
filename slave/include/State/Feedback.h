#pragma once

#include <Arduino.h>

#include "State/SystemState.h"

// How one output (LED, buzzer, vibration motor) behaves.
//   Off:    always off.
//   Steady: on for as long as the pattern plays.
//   Blink:  onMs on, offMs off, `repeats` times; repeats == 0 repeats forever.
struct Pattern {
    enum class Kind : uint8_t { Off, Steady, Blink };

    Kind kind = Kind::Off;
    uint16_t onMs = 0;
    uint16_t offMs = 0;
    uint8_t repeats = 0;

    static constexpr Pattern off() { return {}; }
    static constexpr Pattern steady() { return {Kind::Steady, 0, 0, 0}; }
    static constexpr Pattern blink(uint16_t onMs, uint16_t offMs, uint8_t repeats = 0) {
        return {Kind::Blink, onMs, offMs, repeats};
    }
};

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const Rgb& other) const { return r == other.r && g == other.g && b == other.b; }
    bool operator!=(const Rgb& other) const { return !(*this == other); }
};

// What all the outputs do together for a state or an event.
struct Feedback {
    Pattern led;
    Rgb color;  // LED color while the LED pattern is "on"
    Pattern buzzer;
    Pattern vibration;
};

// The whole behavior of the belt's outputs lives in the two tables below:
// to change a beep or a blink, edit a line here.
namespace FeedbackColor {
// The onboard LED is very bright at full scale; these keep it readable.
constexpr Rgb kBlue{0, 0, 32};
constexpr Rgb kRed{32, 0, 0};
constexpr Rgb kGreen{0, 32, 0};
}  // namespace FeedbackColor

// Plays for as long as the state lasts (repeats == 0). Idle: everything off.
constexpr Feedback feedbackFor(SystemState state) {
    switch (state) {
        case SystemState::PairingSearching:
            return {Pattern::blink(150, 150), FeedbackColor::kBlue, Pattern::off(), Pattern::off()};
        case SystemState::PairingWaiting:
            return {Pattern::blink(500, 500), FeedbackColor::kBlue, Pattern::off(), Pattern::off()};
        case SystemState::AlertHolding:
            return {Pattern::steady(), FeedbackColor::kRed, Pattern::off(), Pattern::off()};
        case SystemState::AlertSending:
            return {Pattern::blink(100, 100), FeedbackColor::kRed, Pattern::off(), Pattern::off()};
        case SystemState::Idle:
            break;
    }
    return {};
}

// Plays once over the state's feedback, only on the outputs it uses (a pattern
// left Off does not touch that output). Every pattern here must be finite.
constexpr Feedback feedbackFor(SystemEvent event) {
    switch (event) {
        case SystemEvent::PairingSucceeded:
            return {Pattern::blink(400, 0, 1), FeedbackColor::kGreen, Pattern::blink(80, 80, 2), Pattern::off()};
        case SystemEvent::PairingTimedOut:
            return {Pattern::off(), {}, Pattern::blink(500, 0, 1), Pattern::off()};
        case SystemEvent::AlertSent:
            return {Pattern::off(), {}, Pattern::off(), Pattern::blink(400, 0, 1)};
        case SystemEvent::AlertRejected:
            return {Pattern::off(), {}, Pattern::off(), Pattern::blink(100, 100, 2)};
        case SystemEvent::BootFault:
            return {Pattern::blink(100, 100, 3), FeedbackColor::kRed, Pattern::off(), Pattern::off()};
    }
    return {};
}
