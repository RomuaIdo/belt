#pragma once

#include <Arduino.h>
#include <vector>
#include "Domain/Notification.h"

// Power-loss-safe storage: persists pending events as individual JSON files on
// LittleFS to prevent data loss or duplicate retries across reboots.

class CallQueueStorage {
public:
    explicit CallQueueStorage(String queueDirPath);

    bool enqueue(const Notification& notification) const;
    bool updatePending(const Notification& notification) const;
    bool remove(const String& eventId) const;

    std::vector<Notification> loadAllPending() const;
    size_t getQueueSize() const;

    // Purges unprocessable queue files: corrupted/truncated ones and fully delivered
    // events left behind by crashes before cleanup. Leaves pending events intact.
    // Returns the count of deleted files; safe to call at boot.
    size_t purgeInvalidEntries() const;

private:
    String queueDirPath;

    String pathFor(const String& eventId) const;
    bool writeToFile(const Notification& notification) const;
};
