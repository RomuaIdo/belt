#pragma once

#include <Arduino.h>
#include <functional>

// What happens after the FallDetector confirms a fall.
//
//   Idle --onFallDetected--> Countdown --cancelWindowMs--> Idle (update() returns true: send the alert)
//   Countdown --cancel (alert button)--> Idle              (false positive, nothing is sent)
//
// The countdown gives the wearer time to cancel a false positive; the
// StateHandler shows it on every output.
class FallService {
public:
    enum class State { Idle, Countdown };
    using NowMsFn = std::function<uint32_t()>;

    FallService(uint32_t cancelWindowMs, NowMsFn nowMs);

    // Starts the countdown. Ignored while one is already running.
    void onFallDetected();

    // Stops a running countdown. Returns true if there was one.
    bool cancel();

    // Call every loop. Returns true exactly once, when the countdown ends uncancelled.
    bool update();

    State getState() const { return state; }
    bool isCountingDown() const { return state == State::Countdown; }

private:
    State state = State::Idle;
    uint32_t cancelWindowMs;
    uint32_t startMs = 0;
    NowMsFn nowMs;
};
