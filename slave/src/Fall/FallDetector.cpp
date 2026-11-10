#include "Fall/FallDetector.h"

#include <string.h>

FallDetector::Result FallDetector::process(const fall::ImuFrame* frames, size_t n, bool lostFrames) {
    if (lostFrames) count = 0;
    if (n > kMaxFramesPerRead) {
        // More than one read can hold: keep the newest, they no longer follow the history.
        frames += n - kMaxFramesPerRead;
        n = kMaxFramesPerRead;
        count = 0;
    }
    memcpy(history + count, frames, n * sizeof(frames[0]));
    const size_t firstNew = count;
    const size_t total = count + n;

    Result result;
    if (total < fall::kWindowFrames) {
        keepNewest(total);
        return result;
    }

    // Every window ending in the new frames, newest first.
    const size_t lastEnd = firstNew + 1 > fall::kWindowFrames ? firstNew + 1 : fall::kWindowFrames;
    size_t bestEnd = 0;
    fall::Features best{};
    for (size_t end = total; end >= lastEnd; end -= kWindowStrideFrames) {
        const fall::Features f = fall::extractFeatures(history + (end - fall::kWindowFrames), fall::kWindowFrames);
        if (end == total) result.features = f;
        if (fall::looksLikeFall(f) && f.peakG > best.peakG) {
            best = f;
            bestEnd = end;
        }
        result.windowsChecked++;
        if (end < lastEnd + kWindowStrideFrames) break;  // size_t: do not wrap below lastEnd
    }

    if (bestEnd != 0) {
        result.candidate = true;
        result.features = best;
        result.probability = fall::fallProbability(history + (bestEnd - fall::kWindowFrames));
        result.fall = fall::modelConfirmsFall(result.probability);
    }

    if (result.fall) {
        count = 0;  // the impact must not be found again in the next read
    } else {
        keepNewest(total);
    }
    return result;
}

// Keeps the newest kWindowFrames so the next read can still form windows ending
// in its first frames.
void FallDetector::keepNewest(size_t total) {
    const size_t keep = total < fall::kWindowFrames ? total : fall::kWindowFrames;
    memmove(history, history + (total - keep), keep * sizeof(history[0]));
    count = keep;
}
