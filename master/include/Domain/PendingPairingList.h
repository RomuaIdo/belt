#pragma once

#include <Arduino.h>
#include <vector>

#include "Domain/PendingPairing.h"

// Belts currently asking to pair. A belt repeats its request every couple of
// seconds, so an entry that stays silent for `ttlMs` is dropped: the list shows
// only belts that are trying right now. Time is passed in to keep it testable.
class PendingPairingList {
public:
    static constexpr size_t kMaxEntries = 8;

    explicit PendingPairingList(uint32_t ttlMs) : ttlMs(ttlMs) {}

    // Adds the belt or refreshes it. When full, the entry silent for the longest
    // makes room. Returns true if the belt was not listed before.
    bool heard(const String& mac, uint32_t nowMs, uint32_t epoch);

    bool contains(const String& mac, uint32_t nowMs);
    bool remove(const String& mac);

    // Live entries, in order of first appearance.
    std::vector<PendingPairing> snapshot(uint32_t nowMs);

private:
    void purgeExpired(uint32_t nowMs);

    uint32_t ttlMs;
    std::vector<PendingPairing> entries;
};
