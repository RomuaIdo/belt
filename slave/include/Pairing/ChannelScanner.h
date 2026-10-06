#pragma once

#include <Arduino.h>
#include <functional>

// Cycles through Wi-Fi channels during broadcast pairing search.
class ChannelScanner {
public:
    using NowMsFn = std::function<uint32_t()>;

    ChannelScanner(uint8_t minChannel, uint8_t maxChannel, uint32_t dwellMs, NowMsFn nowMs);

    void reset(); // Resets to minChannel and restarts dwell timer.

    // Returns true when channel advances to the next one.
    bool tick();

    uint8_t currentChannel() const { return channel; }

private:
    uint8_t minChannel;
    uint8_t maxChannel;
    uint8_t channel;
    uint32_t dwellMs;
    uint32_t lastSwitchMs = 0;
    NowMsFn nowMs;
};
