#include "Domain/PeerNode.h"
#include <algorithm>

PeerNode::PeerNode(String macAddress, String alias)
    : macAddress(std::move(macAddress)), alias(std::move(alias)) {}

void PeerNode::addPhone(const String& phone) {
    for (const auto& existing : phoneNumbers) {
        if (existing == phone) return; // avoid duplicate recipients
    }
    phoneNumbers.push_back(phone);
}

void PeerNode::removePhone(const String& phone) {
    phoneNumbers.erase(
        std::remove(phoneNumbers.begin(), phoneNumbers.end(), phone),
        phoneNumbers.end());
}
