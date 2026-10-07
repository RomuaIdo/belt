// Diagnostic: I2C bus scan on pins 8/9, in both SDA/SCL orders, to find out why the
// MPU-6050 does not answer. Independent of the Mpu6050 driver (uses Wire directly).
// Run: pio test -e esp32-s3-supermini-test -f test_i2c_scan

#include <Arduino.h>
#include <Wire.h>
#include <unity.h>

namespace {
constexpr uint8_t kPinA = 8;
constexpr uint8_t kPinB = 9;
constexpr uint32_t kScanHz = 100000;  // slow and tolerant, unlike the 400 kHz of the app

uint8_t devicesFound = 0;

// Idle level of the lines with no internal pull-up: 1/1 means the external pull-ups work.
void printIdleLevels(uint8_t sda, uint8_t scl) {
    pinMode(sda, INPUT);
    pinMode(scl, INPUT);
    delay(5);
    Serial.printf("  idle level: SDA(GPIO%u)=%d SCL(GPIO%u)=%d\n", sda, digitalRead(sda), scl, digitalRead(scl));
}

void scan(uint8_t sda, uint8_t scl) {
    Serial.printf("\n--- SDA=GPIO%u SCL=GPIO%u @ %lu Hz ---\n", sda, scl, static_cast<unsigned long>(kScanHz));
    printIdleLevels(sda, scl);

    Wire.begin(sda, scl, kScanHz);
    uint8_t errors[8] = {0};  // endTransmission codes: 2=addr NACK, 3=data NACK, 4=other, 5=timeout
    uint8_t found = 0;
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        Wire.beginTransmission(addr);
        const uint8_t code = Wire.endTransmission();
        if (code == 0) {
            Serial.printf("  device found at 0x%02X\n", addr);
            found++;
        } else if (code < sizeof(errors)) {
            errors[code]++;
        }
    }
    Wire.end();

    Serial.printf("  devices: %u | addr NACK: %u | data NACK: %u | other: %u | timeout: %u\n",
                  found, errors[2], errors[3], errors[4], errors[5]);
    devicesFound += found;
}
}  // namespace

void test_some_device_answers() {
    scan(kPinA, kPinB);
    scan(kPinB, kPinA);
    TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(0, devicesFound, "no I2C device answered in either pin order");
}

void setup() {
    delay(2000);  // Allow serial monitor to attach
    Serial.begin(115200);
    Serial.println("\n================ I2C SCAN ================");

    UNITY_BEGIN();
    RUN_TEST(test_some_device_answers);
    UNITY_END();
}

void loop() {}
