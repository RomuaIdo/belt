#include "Domain/PendingPairingList.h"

#include <algorithm>

#include "Util/MacUtils.h"

void PendingPairingList::purgeExpired(uint32_t nowMs) {
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const PendingPairing& entry) {
                                     return static_cast<uint32_t>(nowMs - entry.lastHeardMs) > ttlMs;
                                 }),
                  entries.end());
}

bool PendingPairingList::heard(const String& mac, uint32_t nowMs, uint32_t epoch) {
    purgeExpired(nowMs);

    for (auto& entry : entries) {
        if (MacUtils::equal(entry.mac, mac)) {
            entry.lastHeardMs = nowMs;
            entry.receivedAtEpoch = epoch;
            return false;
        }
    }

    if (entries.size() >= kMaxEntries) {
        // Longest silence = largest elapsed time since the last request.
        entries.erase(std::max_element(entries.begin(), entries.end(),
                                       [&](const PendingPairing& a, const PendingPairing& b) {
                                           return static_cast<uint32_t>(nowMs - a.lastHeardMs) <
                                                  static_cast<uint32_t>(nowMs - b.lastHeardMs);
                                       }));
    }

    PendingPairing entry;
    entry.mac = mac;
    entry.lastHeardMs = nowMs;
    entry.receivedAtEpoch = epoch;
    entries.push_back(entry);
    return true;
}

bool PendingPairingList::contains(const String& mac, uint32_t nowMs) {
    purgeExpired(nowMs);
    return std::any_of(entries.begin(), entries.end(),
                       [&](const PendingPairing& entry) { return MacUtils::equal(entry.mac, mac); });
}

bool PendingPairingList::remove(const String& mac) {
    const size_t before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const PendingPairing& entry) { return MacUtils::equal(entry.mac, mac); }),
                  entries.end());
    return entries.size() != before;
}

std::vector<PendingPairing> PendingPairingList::snapshot(uint32_t nowMs) {
    purgeExpired(nowMs);
    return entries;
}
