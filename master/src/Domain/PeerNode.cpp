#include "Domain/PeerNode.h"
#include <algorithm>

PeerNode::PeerNode(String macAddress, String alias)
    : macAddress(std::move(macAddress)), alias(std::move(alias)) {}

void PeerNode::addChatId(const String& chatId) {
    for (const auto& existing : chatIds) {
        if (existing == chatId) return; // Avoid duplicates
    }
    chatIds.push_back(chatId);
}

void PeerNode::removeChatId(const String& chatId) {
    chatIds.erase(std::remove(chatIds.begin(), chatIds.end(), chatId), chatIds.end());
}
