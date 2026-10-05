#pragma once

#include <Arduino.h>

// Debounce fed raw digitalRead()/millis() samples, no GPIO calls inside —
// generic (not pairing-specific) since it's reused for the cancel button later.
class Button {
public:
    explicit Button(uint32_t debounceMs = 30, bool activeLow = true);

    // True exactly once per debounced press edge (a one-shot event, not a level).
    bool update(bool rawLevel, uint32_t nowMs);

private:
    uint32_t debounceMs;
    bool activeLow;
    bool lastRawLevel;
    bool stableState;
    uint32_t lastEdgeMs = 0;
};
