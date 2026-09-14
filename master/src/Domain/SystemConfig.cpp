#include "Domain/SystemConfig.h"
#include "Util/MacUtils.h"
#include <algorithm>

PeerNode* SystemConfig::findPeerByMac(const String& mac) {
    for (auto& peer : registeredPeers) {
        if (MacUtils::equal(peer.getMacAddress(), mac)) return &peer;
    }
    return nullptr;
}

const PeerNode* SystemConfig::findPeerByMac(const String& mac) const {
    for (const auto& peer : registeredPeers) {
        if (MacUtils::equal(peer.getMacAddress(), mac)) return &peer;
    }
    return nullptr;
}

bool SystemConfig::addPeer(const PeerNode& peer) {
    if (findPeerByMac(peer.getMacAddress()) != nullptr) return false;
    registeredPeers.push_back(peer);
    return true;
}

bool SystemConfig::removePeer(const String& mac) {
    auto it = std::find_if(registeredPeers.begin(), registeredPeers.end(),
                            [&](const PeerNode& peer) {
                                return MacUtils::equal(peer.getMacAddress(), mac);
                            });
    if (it == registeredPeers.end()) return false;
    registeredPeers.erase(it);
    return true;
}

bool SystemConfig::isConfigured() const {
    return wifiSsid.length() > 0 && telegramBotToken.length() > 0;
}
