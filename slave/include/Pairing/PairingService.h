#pragma once

#include <Arduino.h>
#include <functional>

// State machine for the belt's pairing attempt.
//
//   Idle --startSearching--> Searching --onPairWait--> Waiting --onPairAccepted--> Paired
//                               ^  |                      |  |
//                               |  +------onPairAccepted---+  |  (master accepted at once)
//                               +-------resumeSearching-------+  (master went silent)
//   Searching/Waiting --timeout--> TimedOut
//
// Searching: scanning channels with broadcast requests.
// Waiting:   a master answered; channel locked, waiting for the caregiver to press Save.
// One timeout covers the whole attempt, since the caregiver needs time to open the page.
class PairingService {
public:
    enum class State { Idle, Searching, Waiting, Paired, TimedOut };
    using NowMsFn = std::function<uint32_t()>;

    PairingService(uint32_t timeoutMs, NowMsFn nowMs);

    void startSearching();
    void tick();

    // Searching/Waiting -> Waiting. No-op in any other state.
    void onPairWait();

    // Waiting -> Searching (keeps the attempt's timeout running).
    void resumeSearching();

    // Searching/Waiting -> Paired. No-op in any other state.
    void onPairAccepted();

    void reset();  // Paired/TimedOut -> Idle

    State getState() const { return state; }
    bool isSearching() const { return state == State::Searching; }
    bool isWaiting() const { return state == State::Waiting; }
    bool isActive() const { return state == State::Searching || state == State::Waiting; }
    uint32_t remainingMs() const;

private:
    State state = State::Idle;
    uint32_t timeoutMs;
    uint32_t startMs = 0;
    NowMsFn nowMs;
};
