#pragma once

#include <Arduino.h>

// Domain model for the slave's single learned peer, discovered via pairing.
class MasterLink {
public:
    MasterLink() = default;
    explicit MasterLink(String macAddress) : macAddress(std::move(macAddress)) {}

    bool hasMac() const { return macAddress.length() > 0; }
    const String& getMacAddress() const { return macAddress; }

private:
    String macAddress;
};
