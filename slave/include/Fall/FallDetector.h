#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Fall/FallPipeline.h"

// Turns the frames read from the IMU FIFO into fall decisions (no hardware).
//
// Every 2 s window ending in the frames just read is checked by the heuristic,
// stepping kWindowStrideFrames. The passing window with the strongest impact goes
// to the CNN. Frames from earlier reads are kept so a window can span two reads.
// After a confirmed fall the history is cleared: the same impact is reported once.
class FallDetector {
public:
    static constexpr size_t kMaxFramesPerRead = 170;  // BMI270 FIFO: 2048 B / 12 B
    static constexpr size_t kWindowStrideFrames = 5;  // a window every 0.1 s of new data

    struct Result {
        bool candidate = false;  // a window passed the heuristic
        bool fall = false;       // the model confirmed it
        float probability = 0.0f;
        size_t windowsChecked = 0;
        fall::Features features{};  // of the candidate, else of the newest window
    };

    // `frames` were read since the previous call, oldest first. `lostFrames`: some
    // frames before these were dropped, so they do not continue the history.
    Result process(const fall::ImuFrame* frames, size_t n, bool lostFrames);

    void reset() { count = 0; }
    size_t historySize() const { return count; }

private:
    void keepNewest(size_t total);

    fall::ImuFrame history[fall::kWindowFrames + kMaxFramesPerRead];
    size_t count = 0;
};
