#include "Input/Button.h"

Button::Button(uint32_t debounceMs, bool activeLow)
    : debounceMs(debounceMs), activeLow(activeLow), lastRawLevel(activeLow), stableState(activeLow) {}

bool Button::update(bool rawLevel, uint32_t nowMs) {
    if (rawLevel != lastRawLevel) {
        lastRawLevel = rawLevel;
        lastEdgeMs = nowMs;
        return false;
    }

    if (static_cast<uint32_t>(nowMs - lastEdgeMs) < debounceMs) {
        return false; // Still within debounce interval
    }

    if (stableState == rawLevel) {
        return false; // State unchanged
    }

    stableState = rawLevel;
    return activeLow ? !rawLevel : rawLevel; // True on press edge
}
