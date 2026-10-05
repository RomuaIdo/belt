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
        return false; // still settling after the last transition
    }

    if (stableState == rawLevel) {
        return false; // already-debounced state hasn't changed
    }

    stableState = rawLevel;
    return activeLow ? !rawLevel : rawLevel; // true only on a press edge, not a release
}
