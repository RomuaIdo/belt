#pragma once

#include <Arduino.h>
#include <vector>


// Domain model (class) representing an authorized remote ESP sensor (slave) node

class PeerNode {
public:
    PeerNode() = default;
    PeerNode(String macAddress, String alias);

    void addPhone(const String& phone);
    void removePhone(const String& phone);

    const std::vector<String>& getPhones() const { return phoneNumbers; }
    const String& getMacAddress() const { return macAddress; }
    const String& getAlias() const { return alias; }
    void setAlias(const String& newAlias) { alias = newAlias; }

private:
    String macAddress;
    String alias;
    std::vector<String> phoneNumbers;
};
