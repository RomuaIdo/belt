#pragma once

#include <Arduino.h>
#include <functional>

// State machine for the alert button hold.
//
//   Idle --onPress--> Holding --held kAlertHoldMs--> Sending
//   Holding/Sending --button released--> Idle
//   Holding/Sending --abort--> Idle   (alert could not be sent)
//
// Holding: button pressed, hold time not reached yet.
// Sending: hold reached and update() returned true once; stays here (one alert
//          per hold) until the button is released.
class AlertService {
public:
    enum class State { Idle, Holding, Sending };
    using NowMsFn = std::function<uint32_t()>;

    AlertService(uint32_t holdMs, NowMsFn nowMs);

    // Press edge of the alert button: starts counting the hold.
    void onPress();

    // Call every loop with the debounced button level. Returns true exactly once
    // per hold, when it is time to send the alert.
    bool update(bool pressed);

    // Gives up the current hold (e.g. no master to send to) until the next press.
    void abort();

    State getState() const { return state; }

private:
    State state = State::Idle;
    uint32_t holdMs;
    uint32_t pressStartMs = 0;
    NowMsFn nowMs;
};
