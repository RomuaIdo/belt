#pragma once

#include <Arduino.h>
#include <functional>

// State machine for slave broadcast pairing search (mode 1).
class PairingService {
public:
    enum class State { Idle, Searching, Paired, TimedOut };
    using NowMsFn = std::function<uint32_t()>;

    PairingService(uint32_t searchTimeoutMs, NowMsFn nowMs);

    void startSearching();
    void tick();

    // Transitions from Searching to Paired.
    void onPairResponseReceived();

    void reset();

    State getState() const { return state; }
    bool isSearching() const { return state == State::Searching; }
    uint32_t remainingMs() const;

private:
    State state = State::Idle;
    uint32_t searchTimeoutMs;
    uint32_t searchStartMs = 0;
    NowMsFn nowMs;
};
