#pragma once

#include <Arduino.h>

#include "State/Feedback.h"

// Pure timing for one Pattern: says whether the output should be on at a given
// time. No hardware and no clock of its own (the caller passes nowMs).
class PatternPlayer {
public:
    void start(const Pattern& newPattern, uint32_t nowMs);

    bool isOn(uint32_t nowMs) const;

    // True once a finite pattern (Blink with repeats > 0) has played all its repeats.
    // Off, Steady and endless Blink never finish.
    bool finished(uint32_t nowMs) const;

private:
    Pattern pattern;
    uint32_t startMs = 0;
};
