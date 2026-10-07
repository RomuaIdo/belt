#include "Sensor/Mpu6050.h"

namespace {
constexpr uint8_t kRegSmplrtDiv = 0x19;
constexpr uint8_t kRegConfig = 0x1A;
constexpr uint8_t kRegGyroConfig = 0x1B;
constexpr uint8_t kRegAccelConfig = 0x1C;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegWhoAmI = 0x75;

constexpr uint8_t kExpectedWhoAmI = 0x68;
constexpr uint8_t kPwrReset = 0x80;
constexpr uint8_t kPwrClockPllGyroX = 0x01;
constexpr uint8_t kDlpf44Hz = 0x03;
constexpr uint8_t kSampleRateDiv100Hz = 9;  // 1 kHz / (1 + 9) with DLPF on
constexpr uint8_t kGyroFs2000 = 0x18;
constexpr uint8_t kAccelFs16g = 0x18;

constexpr uint32_t kResetDelayMs = 100;
constexpr size_t kBurstLen = 14;  // accel(6) + temp(2) + gyro(6)
}  // namespace

Mpu6050::Mpu6050(TwoWire& wire, uint8_t address) : wire(wire), address(address) {}

bool Mpu6050::begin() {
    const uint8_t id = whoAmI();
    if (id != kExpectedWhoAmI) {
        Serial.printf("Mpu6050: unexpected WHO_AM_I 0x%02X (expected 0x%02X)\n", id, kExpectedWhoAmI);
        return false;
    }

    if (!writeReg(kRegPwrMgmt1, kPwrReset)) return false;
    delay(kResetDelayMs);
    if (!writeReg(kRegPwrMgmt1, kPwrClockPllGyroX)) return false;  // wakes from sleep
    delay(kResetDelayMs);  // sensors need time to start before the first valid sample

    if (!writeReg(kRegConfig, kDlpf44Hz)) return false;
    if (!writeReg(kRegSmplrtDiv, kSampleRateDiv100Hz)) return false;
    if (!writeReg(kRegGyroConfig, kGyroFs2000)) return false;
    if (!writeReg(kRegAccelConfig, kAccelFs16g)) return false;

    // Read the config back: a wrong range would silently saturate on impact.
    struct Check { uint8_t reg; uint8_t expected; };
    const Check checks[] = {
        {kRegConfig, kDlpf44Hz},
        {kRegSmplrtDiv, kSampleRateDiv100Hz},
        {kRegGyroConfig, kGyroFs2000},
        {kRegAccelConfig, kAccelFs16g},
    };
    for (const Check& c : checks) {
        uint8_t value = 0;
        if (!readRegs(c.reg, &value, 1) || value != c.expected) {
            Serial.printf("Mpu6050: register 0x%02X read back 0x%02X, expected 0x%02X\n", c.reg, value, c.expected);
            return false;
        }
    }
    return true;
}

bool Mpu6050::readSample(ImuSample& out) {
    uint8_t buf[kBurstLen];
    if (!readRegs(kRegAccelXoutH, buf, sizeof(buf))) return false;

    // Not named "word": Arduino.h defines a word() macro.
    auto be16 = [&buf](size_t i) { return static_cast<int16_t>((buf[i] << 8) | buf[i + 1]); };
    out.ax = be16(0);
    out.ay = be16(2);
    out.az = be16(4);
    // bytes 6..7 are the temperature, discarded
    out.gx = be16(8);
    out.gy = be16(10);
    out.gz = be16(12);
    out.tMs = millis();
    return true;
}

uint8_t Mpu6050::whoAmI() {
    uint8_t id = 0xFF;
    if (!readRegs(kRegWhoAmI, &id, 1)) return 0xFF;
    return id;
}

bool Mpu6050::writeReg(uint8_t reg, uint8_t value) {
    wire.beginTransmission(address);
    wire.write(reg);
    wire.write(value);
    return wire.endTransmission() == 0;
}

bool Mpu6050::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    wire.beginTransmission(address);
    wire.write(reg);
    if (wire.endTransmission(false) != 0) return false;  // repeated start

    if (wire.requestFrom(address, static_cast<uint8_t>(len)) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = wire.read();
    return true;
}
