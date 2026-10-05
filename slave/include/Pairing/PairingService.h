#pragma once

#include <Arduino.h>
#include <functional>

// Pure state machine for the broadcast-search pairing flow (mode 1):
// button press -> Searching (bounded by searchTimeoutMs) -> Paired once
// the master's PairResponse arrives, or TimedOut. Hardware I/O (channel
// hopping, actually broadcasting) is handled externally by AppController.
class PairingService {
public:
    enum class State { Idle, Searching, Paired, TimedOut };
    using NowMsFn = std::function<uint32_t()>;

    PairingService(uint32_t searchTimeoutMs, NowMsFn nowMs);

    void startSearching();
    void tick();

    // Searching -> Paired; a no-op in any other state.
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
