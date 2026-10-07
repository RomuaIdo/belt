#pragma once

#include <Arduino.h>

// One raw reading: registers already assembled from big-endian bytes.
struct ImuSample {
    int16_t ax, ay, az;  // accelerometer, raw LSB
    int16_t gx, gy, gz;  // gyroscope, raw LSB
    uint32_t tMs;        // millis() when the sample was read
};
