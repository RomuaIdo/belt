#pragma once

#include <Arduino.h>
#include <functional>

// Tracks the wait for one belt's PairConfirm while Save runs, and its timeout.
class PairingService {
public:
    using NowMsFn = std::function<uint32_t()>;

    PairingService(uint32_t confirmTimeoutMs, NowMsFn nowMs);

    void start(const String& targetMac); // Awaits targetMac confirmation
    void stop();
    void tick(); // Deactivates on timeout expiry
    void confirm(); // Completes the wait when the belt's PairConfirm arrives

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
