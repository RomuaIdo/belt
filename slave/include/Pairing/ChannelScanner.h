#pragma once

#include <Arduino.h>
#include <functional>

// Cycles through ESP-NOW channels while broadcast-searching for a master
// whose channel isn't known in advance (it inherits whatever channel the
// caregiver's home Wi-Fi router forced the master's AP onto). Pure timing;
// AppController applies the channel via EspNowTransceiver::setChannel()
// and re-broadcasts whenever this advances.
class ChannelScanner {
public:
    using NowMsFn = std::function<uint32_t()>;

    ChannelScanner(uint8_t minChannel, uint8_t maxChannel, uint32_t dwellMs, NowMsFn nowMs);

    void reset(); // back to minChannel, restarts the dwell timer

    // Call every loop() while searching. True exactly on the tick where
    // the channel just advanced (caller should re-broadcast there).
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
