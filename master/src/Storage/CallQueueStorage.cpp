#include "Storage/CallQueueStorage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {

// Attempts to reconstruct a QueuedNotification from a queue file's raw
// content.
bool tryParseNotification(File& entry, QueuedNotification& out) {
    JsonDocument doc;
    if (deserializeJson(doc, entry)) return false;

    String eventId = doc["eventId"] | "";
    if (eventId.isEmpty()) return false;

    uint32_t timestamp = doc["timestamp"] | 0;
    String originMac = doc["originMac"] | "";
    String message = doc["message"] | "";

    std::vector<String> pendingPhones;
    JsonArrayConst pending = doc["pendingPhones"].as<JsonArrayConst>();
    pendingPhones.reserve(pending.size());
    for (const char* phone : pending) {
        if (phone) pendingPhones.emplace_back(phone);
    }

    out = QueuedNotification(eventId, timestamp, originMac, message, pendingPhones);
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

bool CallQueueStorage::writeToFile(const QueuedNotification& notification) const {
    JsonDocument doc;
    doc["eventId"] = notification.getEventId();
    doc["timestamp"] = notification.getTimestamp();
    doc["originMac"] = notification.getOriginMac();
    doc["message"] = notification.getMessage();

    JsonArray pending = doc["pendingPhones"].to<JsonArray>();
    for (const auto& phone : notification.getPendingPhones()) {
        pending.add(phone);
    }

    File file = LittleFS.open(pathFor(notification.getEventId()), "w");
    if (!file) return false;
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

bool CallQueueStorage::enqueue(const QueuedNotification& notification) const {
    return writeToFile(notification);
}

bool CallQueueStorage::updatePending(const QueuedNotification& notification) const {
    // Same on-disk representation as enqueue(): overwrite the event's file
    // with the new (shorter) pendingPhones list. Deciding when an event is
    // done and should be removed instead is the caller's job (see
    // QueueWorker), not this storage layer's.
    return writeToFile(notification);
}

bool CallQueueStorage::remove(const String& eventId) const {
    String path = pathFor(eventId);
    if (!LittleFS.exists(path)) return true;
    return LittleFS.remove(path);
}

std::vector<QueuedNotification> CallQueueStorage::loadAllPending() const {
    std::vector<QueuedNotification> result;

    File dir = LittleFS.open(queueDirPath);
    if (!dir || !dir.isDirectory()) return result;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            QueuedNotification notification;
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

    // Collect the paths to delete first, then delete after closing the
    // directory listing: mutating a directory mid-iteration isn't safe.
    std::vector<String> pathsToRemove;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String path = entry.path();
            QueuedNotification notification;
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
