#pragma once

#include <Arduino.h>
#include <functional>

// Tracks a single in-progress pairing attempt started from the dashboard
// (the caregiver enters a belt's MAC; master unicasts a PairRequest and
// waits here for its PairResponse). Pure state/timing, driven by an
// injected clock, so it stays unit-testable without real ESP-NOW traffic.
class PairingService {
public:
    using NowMsFn = std::function<uint32_t()>;

    PairingService(uint32_t confirmTimeoutMs, NowMsFn nowMs);

    void start(const String& targetMac); // begins waiting for targetMac's confirmation
    void stop();
    void tick(); // auto-deactivates once confirmTimeoutMs elapses unconditionally
    void confirm(); // call once the awaited PairResponse arrives

    bool isActive() const { return active; }
    bool isPendingFor(const String& mac) const;
    const String& getTargetMac() const { return targetMac; }
    uint32_t remainingMs() const;

private:
    uint32_t confirmTimeoutMs;
    uint32_t startMs = 0;
    bool active = false;
    String targetMac;
    NowMsFn nowMs;
};
