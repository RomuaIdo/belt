#pragma once

#include <Arduino.h>
#include <array>

#include "Domain/ImuSample.h"

// Fixed-size circular buffer of the most recent samples (no heap allocation).
// Once full, each push overwrites the oldest sample.
template <size_t N>
class SampleBuffer {
public:
    void push(const ImuSample& sample) {
        data[head] = sample;
        head = (head + 1) % N;
        if (count < N) count++;
    }

    size_t size() const { return count; }
    bool isFull() const { return count == N; }
    static constexpr size_t capacity() { return N; }

    // 0 = oldest, size() - 1 = newest. The index must be below size().
    const ImuSample& at(size_t i) const { return data[(head + N - count + i) % N]; }

    // Newest sample. Requires size() > 0.
    const ImuSample& latest() const { return at(count - 1); }

    void clear() {
        head = 0;
        count = 0;
    }

private:
    std::array<ImuSample, N> data{};
    size_t head = 0;  // next write position
    size_t count = 0;
};
