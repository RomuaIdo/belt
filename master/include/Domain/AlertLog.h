#pragma once

#include <Arduino.h>
#include <vector>

// Recent alerts and how their Telegram sends went, for the config page. RAM only:
// it is cleared on reboot (alerts still in the queue are added back at startup).
// Not thread-safe: use from the loop() task only.
class AlertLog {
public:
    // Every alert is its own entry (repeated falls of the same belt included). The
    // cap only bounds RAM (~200 bytes per entry); the oldest goes first.
    static constexpr size_t CAPACITY = 50;

    enum class Outcome : uint8_t {
        Queued,        // has recipients; see each chat's status
        NoRecipients,  // registered belt with no chats: nothing sent
        UnknownBelt,   // valid frame from a belt not registered here: nothing sent
    };

    enum class ChatStatus : uint8_t {
        Sending,   // not sent yet (waiting for Wi-Fi/clock, or in flight)
        Sent,      // Telegram accepted it (HTTP 2xx)
        Rejected,  // Telegram refused this chat for good (HTTP 400/403)
        Retrying,  // last try failed (no network, 5xx, ...); it will be tried again
    };

    struct Chat {
        String chatId;
        ChatStatus status = ChatStatus::Sending;
        int httpStatus = 0;  // last HTTP code (or negative error); 0 = none yet
        uint32_t deliveredAtEpoch = 0;  // when Telegram accepted it; 0 = not delivered
    };

    struct Entry {
        String id;
        String mac;
        String name;                   // belt alias, or its MAC
        uint32_t receivedAtEpoch = 0;  // 0 if the clock was not synced
        Outcome outcome = Outcome::Queued;
        std::vector<Chat> chats;
    };

    // Adds an alert; the oldest is dropped once CAPACITY is reached. An existing
    // entry with the same id is replaced.
    void add(Entry entry) {
        remove(entry.id);
        if (entries_.size() >= CAPACITY) entries_.erase(entries_.begin());
        entries_.push_back(std::move(entry));
    }

    // Updates one chat of an alert; ignored if either is not in the log. `atEpoch` is
    // kept as the delivery time when the status is Sent.
    void setChatStatus(const String& id, const String& chatId, ChatStatus status, int httpStatus,
                       uint32_t atEpoch = 0) {
        Entry* entry = find(id);
        if (entry == nullptr) return;
        for (auto& chat : entry->chats) {
            if (chat.chatId == chatId) {
                chat.status = status;
                chat.httpStatus = httpStatus;
                chat.deliveredAtEpoch = status == ChatStatus::Sent ? atEpoch : 0;
                return;
            }
        }
    }

    // Returns false if the id is not in the log.
    bool remove(const String& id) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == id) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }

    void clear() { entries_.clear(); }

    bool contains(const String& id) const {
        for (const auto& entry : entries_) {
            if (entry.id == id) return true;
        }
        return false;
    }

    // Oldest first.
    const std::vector<Entry>& entries() const { return entries_; }

private:
    Entry* find(const String& id) {
        for (auto& entry : entries_) {
            if (entry.id == id) return &entry;
        }
        return nullptr;
    }

    std::vector<Entry> entries_;
};
