#pragma once

#include <Arduino.h>

// Generic debounce handler based on raw level samples.
class Button {
public:
    explicit Button(uint32_t debounceMs = 30, bool activeLow = true);

    // Returns true once per debounced press edge.
    bool update(bool rawLevel, uint32_t nowMs);

private:
    uint32_t debounceMs;
    bool activeLow;
    bool lastRawLevel;
    bool stableState;
    uint32_t lastEdgeMs = 0;
};
