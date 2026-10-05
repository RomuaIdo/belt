#pragma once

#include <Arduino.h>
#include <functional>

// Pure state machine for time-windowed pairing. 
// Hardware I/O and persistence are handled externally.
class PairingService {
public:
    enum class State { Idle, Listening, Paired, TimedOut };
    using NowMsFn = std::function<uint32_t()>;

    PairingService(uint32_t listenWindowMs, NowMsFn nowMs);

    void startListening();
    void tick();
    
    // Ignored unless in State::Listening
    void onPairRequestReceived();
    
    void reset();

    State getState() const { return state; }
    bool isListening() const { return state == State::Listening; }
    uint32_t remainingMs() const;

private:
    State state = State::Idle;
    uint32_t listenWindowMs;
    uint32_t windowStartMs = 0;
    NowMsFn nowMs;
};
