#pragma once

#include <Arduino.h>
#include <vector>

class Notification {
public:
    Notification() = default;
    Notification(String eventId, uint32_t timestamp, String originMac,
                 String message, std::vector<String> pendingChatIds);

    bool isCompleted() const { return pendingChatIds.empty(); }
    void markChatAsSent(const String& chatId);

    const std::vector<String>& getPendingChatIds() const { return pendingChatIds; }
    const String& getEventId() const { return eventId; }
    uint32_t getTimestamp() const { return timestamp; }
    const String& getOriginMac() const { return originMac; }
    const String& getMessage() const { return message; }

    static String makeEventId(const String& originMac, uint32_t timestamp);

private:
    String eventId;
    uint32_t timestamp = 0;
    String originMac;
    String message;
    std::vector<String> pendingChatIds;
};
