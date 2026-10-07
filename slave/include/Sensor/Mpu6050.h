#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "Domain/ImuSample.h"

// MPU-6050 driver over I2C (Wire), no external library.
// Configured as the project requires: +-16 g, +-2000 deg/s, 100 Hz with DLPF.
// The caller owns the bus: call Wire.begin() before begin().
class Mpu6050 {
public:
    static constexpr float kAccelLsbPerG = 2048.0f;  // +-16 g
    static constexpr float kGyroLsbPerDps = 16.4f;   // +-2000 deg/s

    explicit Mpu6050(TwoWire& wire, uint8_t address = 0x68);  // 0x68 = AD0 low

    // Checks WHO_AM_I, resets, applies the config and reads it back.
    bool begin();

    // Burst read of accel + gyro (temperature is discarded).
    bool readSample(ImuSample& out);

    // WHO_AM_I register, or 0xFF if the device did not answer.
    uint8_t whoAmI();

    static float toG(int16_t raw) { return raw / kAccelLsbPerG; }
    static float toDps(int16_t raw) { return raw / kGyroLsbPerDps; }

private:
    bool writeReg(uint8_t reg, uint8_t value);
    bool readRegs(uint8_t reg, uint8_t* buf, size_t len);

    TwoWire& wire;
    uint8_t address;
};
