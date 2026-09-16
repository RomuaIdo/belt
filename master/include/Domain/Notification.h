#pragma once

#include <Arduino.h>
#include <vector>

class Notification {
public:
    Notification() = default;
    Notification(String eventId, uint32_t timestamp, String originMac,
                 String message, std::vector<String> pendingPhones);

    bool isCompleted() const { return pendingPhones.empty(); }
    void markPhoneAsSent(const String& phone);

    const std::vector<String>& getPendingPhones() const { return pendingPhones; }
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
    std::vector<String> pendingPhones;
};
