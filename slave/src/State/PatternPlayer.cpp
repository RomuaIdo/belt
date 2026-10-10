#include "State/PatternPlayer.h"

void PatternPlayer::start(const Pattern& newPattern, uint32_t nowMs) {
    pattern = newPattern;
    startMs = nowMs;
}

bool PatternPlayer::finished(uint32_t nowMs) const {
    if (pattern.kind != Pattern::Kind::Blink || pattern.repeats == 0) return false;

    const uint32_t cycleMs = static_cast<uint32_t>(pattern.onMs) + pattern.offMs;
    const uint32_t elapsed = static_cast<uint32_t>(nowMs - startMs);
    return elapsed >= cycleMs * pattern.repeats;
}

bool PatternPlayer::isOn(uint32_t nowMs) const {
    switch (pattern.kind) {
        case Pattern::Kind::Off:
            return false;
        case Pattern::Kind::Steady:
            return true;
        case Pattern::Kind::Blink: {
            if (finished(nowMs)) return false;
            const uint32_t cycleMs = static_cast<uint32_t>(pattern.onMs) + pattern.offMs;
            if (cycleMs == 0) return false;
            const uint32_t elapsed = static_cast<uint32_t>(nowMs - startMs);
            return (elapsed % cycleMs) < pattern.onMs;
        }
    }
    return false;
}
