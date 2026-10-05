#include "Pairing/ChannelScanner.h"

ChannelScanner::ChannelScanner(uint8_t minChannel, uint8_t maxChannel, uint32_t dwellMs, NowMsFn nowMs)
    : minChannel(minChannel), maxChannel(maxChannel), channel(minChannel),
      dwellMs(dwellMs), nowMs(std::move(nowMs)) {}

void ChannelScanner::reset() {
    channel = minChannel;
    lastSwitchMs = nowMs();
}

bool ChannelScanner::tick() {
    if (static_cast<uint32_t>(nowMs() - lastSwitchMs) < dwellMs) return false;

    lastSwitchMs = nowMs();
    channel = (channel >= maxChannel) ? minChannel : static_cast<uint8_t>(channel + 1);
    return true;
}
