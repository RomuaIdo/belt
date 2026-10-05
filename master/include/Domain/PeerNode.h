#pragma once

#include <Arduino.h>
#include <vector>


// Domain model (class) representing an authorized remote ESP sensor (slave) node

class PeerNode {
public:
    PeerNode() = default;
    PeerNode(String macAddress, String alias);

    void addChatId(const String& chatId);
    void removeChatId(const String& chatId);

    const std::vector<String>& getChatIds() const { return chatIds; }
    const String& getMacAddress() const { return macAddress; }
    const String& getAlias() const { return alias; }
    void setAlias(const String& newAlias) { alias = newAlias; }

    // Modelo do texto do alerta, com placeholders ({nome}, {hora}, ...).
    // Vazio significa "usar a mensagem padrao".
    const String& getMessage() const { return message; }
    void setMessage(const String& newMessage) { message = newMessage; }

private:
    String macAddress;
    String alias;
    String message;
    std::vector<String> chatIds;
};
