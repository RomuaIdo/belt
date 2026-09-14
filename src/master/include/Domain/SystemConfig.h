#pragma once

#include <Arduino.h>
#include <vector>
#include "Domain/PeerNode.h"

// In-memory representation of the full operational configuration.

class SystemConfig {
public:
    String wifiSsid;
    String wifiPassword;
    String telegramBotToken;

    PeerNode* findPeerByMac(const String& mac);
    const PeerNode* findPeerByMac(const String& mac) const;

    bool addPeer(const PeerNode& peer);
    bool removePeer(const String& mac);

    const std::vector<PeerNode>& getPeers() const { return registeredPeers; }

    bool isConfigured() const;

private:
    std::vector<PeerNode> registeredPeers;
};
