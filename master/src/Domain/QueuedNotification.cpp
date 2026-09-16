#include "Domain/QueuedNotification.h"
#include <algorithm>

QueuedNotification::QueuedNotification(String eventId, uint32_t timestamp, String originMac,
                                         String message, std::vector<String> pendingPhones)
    : eventId(std::move(eventId)),
      timestamp(timestamp),
      originMac(std::move(originMac)),
      message(std::move(message)),
      pendingPhones(std::move(pendingPhones)) {}

void QueuedNotification::markPhoneAsSent(const String& phone) {
    pendingPhones.erase(
        std::remove(pendingPhones.begin(), pendingPhones.end(), phone),
        pendingPhones.end());
}

String QueuedNotification::makeEventId(const String& originMac, uint32_t timestamp) {
    String id = originMac;
    id.replace(":", "");
    id += "_";
    id += String(timestamp);
    return id;
}
