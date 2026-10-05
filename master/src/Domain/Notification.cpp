#include "Domain/Notification.h"
#include <algorithm>

Notification::Notification(String eventId, uint32_t timestamp, String originMac,
                            String message, std::vector<String> pendingChatIds)
    : eventId(std::move(eventId)),
      timestamp(timestamp),
      originMac(std::move(originMac)),
      message(std::move(message)),
      pendingChatIds(std::move(pendingChatIds)) {}

void Notification::markChatAsSent(const String& chatId) {
    pendingChatIds.erase(
        std::remove(pendingChatIds.begin(), pendingChatIds.end(), chatId),
        pendingChatIds.end());
}

String Notification::makeEventId(const String& originMac, uint32_t timestamp) {
    String id = originMac;
    id.replace(":", "");
    id += "_";
    id += String(timestamp);
    return id;
}
