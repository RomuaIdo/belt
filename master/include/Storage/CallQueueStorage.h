#pragma once

#include <Arduino.h>
#include <vector>
#include "Domain/Notification.h"

// LittleFS persistence for pending alert notifications across reboots.

class CallQueueStorage {
public:
    explicit CallQueueStorage(String queueDirPath);

    bool enqueue(const Notification& notification) const;
    bool updatePending(const Notification& notification) const;
    bool remove(const String& eventId) const;

    std::vector<Notification> loadAllPending() const;
    size_t getQueueSize() const;

    // Purges corrupted or fully delivered queue files; returns count of deleted files.
    size_t purgeInvalidEntries() const;

private:
    String queueDirPath;

    String pathFor(const String& eventId) const;
    bool writeToFile(const Notification& notification) const;
};
