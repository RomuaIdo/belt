#include "Storage/CallQueueStorage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {

// Parses a Notification from raw JSON queue content.
bool tryParseNotification(File& entry, Notification& out) {
    JsonDocument doc;
    if (deserializeJson(doc, entry)) return false;

    String eventId = doc["eventId"] | "";
    if (eventId.isEmpty()) return false;

    uint32_t timestamp = doc["timestamp"] | 0;
    String originMac = doc["originMac"] | "";
    String message = doc["message"] | "";

    std::vector<String> pendingChatIds;
    JsonArrayConst pending = doc["pendingChatIds"].as<JsonArrayConst>();
    pendingChatIds.reserve(pending.size());
    for (const char* chatId : pending) {
        if (chatId) pendingChatIds.emplace_back(chatId);
    }

    out = Notification(eventId, timestamp, originMac, message, pendingChatIds);
    return true;
}

} // namespace

CallQueueStorage::CallQueueStorage(String queueDirPath) : queueDirPath(std::move(queueDirPath)) {
    if (!LittleFS.exists(this->queueDirPath)) {
        LittleFS.mkdir(this->queueDirPath);
    }
}

String CallQueueStorage::pathFor(const String& eventId) const {
    return queueDirPath + "/evt_" + eventId + ".json";
}

bool CallQueueStorage::writeToFile(const Notification& notification) const {
    JsonDocument doc;
    doc["eventId"] = notification.getEventId();
    doc["timestamp"] = notification.getTimestamp();
    doc["originMac"] = notification.getOriginMac();
    doc["message"] = notification.getMessage();

    JsonArray pending = doc["pendingChatIds"].to<JsonArray>();
    for (const auto& chatId : notification.getPendingChatIds()) {
        pending.add(chatId);
    }
    
    File file = LittleFS.open(pathFor(notification.getEventId()), "w");
    if (!file) return false;
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

bool CallQueueStorage::enqueue(const Notification& notification) const {
    return writeToFile(notification);
}

bool CallQueueStorage::updatePending(const Notification& notification) const {
    // Overwrites event file with updated pending chat IDs list.
    return writeToFile(notification);
}

bool CallQueueStorage::remove(const String& eventId) const {
    String path = pathFor(eventId);
    if (!LittleFS.exists(path)) return true;
    return LittleFS.remove(path);
}

std::vector<Notification> CallQueueStorage::loadAllPending() const {
    std::vector<Notification> result;

    File dir = LittleFS.open(queueDirPath);
    if (!dir || !dir.isDirectory()) return result;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            Notification notification;
            if (tryParseNotification(entry, notification) && !notification.isCompleted()) {
                result.push_back(notification);
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    return result;
}

size_t CallQueueStorage::purgeInvalidEntries() const {
    File dir = LittleFS.open(queueDirPath);
    if (!dir || !dir.isDirectory()) return 0;

    // Collect paths first; mutating a directory during iteration is unsafe.
    std::vector<String> pathsToRemove;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String path = entry.path();
            Notification notification;
            bool parsed = tryParseNotification(entry, notification);

            if (!parsed) {
                Serial.printf("CallQueueStorage: purging corrupted queue file %s (unparseable)\n", path.c_str());
                pathsToRemove.push_back(path);
            } else if (notification.isCompleted()) {
                Serial.printf("CallQueueStorage: purging orphaned queue file %s (already delivered, never removed)\n",
                               path.c_str());
                pathsToRemove.push_back(path);
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    size_t removedCount = 0;
    for (const auto& path : pathsToRemove) {
        if (LittleFS.remove(path)) ++removedCount;
    }
    return removedCount;
}

size_t CallQueueStorage::getQueueSize() const {
    size_t count = 0;
    File dir = LittleFS.open(queueDirPath);
    if (!dir || !dir.isDirectory()) return 0;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) ++count;
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    return count;
}
