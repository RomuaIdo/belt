#pragma once

#include <Arduino.h>

// Domain model for the paired master peer: its MAC and the Wi-Fi channel it was on
// when it accepted the pairing (the router's channel; 0 = unknown).
class MasterLink {
public:
    MasterLink() = default;
    MasterLink(String macAddress, uint8_t channel)
        : macAddress(std::move(macAddress)), channel(channel) {}

    bool hasMac() const { return macAddress.length() > 0; }
    bool hasChannel() const { return channel != 0; }
    const String& getMacAddress() const { return macAddress; }
    uint8_t getChannel() const { return channel; }

private:
    String macAddress;
    uint8_t channel = 0;
};
