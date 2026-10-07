// Hardware test: MPU-6050 over I2C. Needs the sensor wired to the pins in AppConfig.
// Keep the board still and flat while it runs.
// Run: pio test -e esp32-s3-supermini-test -f test_mpu6050

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <unity.h>

#include "Sensor/Mpu6050.h"
#include "Util/AppConfig.h"

namespace {
Mpu6050 mpu(Wire);
}

void test_who_am_i() {
    const uint8_t id = mpu.whoAmI();
    Serial.printf("WHO_AM_I: 0x%02X\n", id);
    TEST_ASSERT_EQUAL_HEX8(0x68, id);
}

void test_begin_applies_config() {
    TEST_ASSERT_TRUE(mpu.begin());
}

void test_read_sample_succeeds() {
    ImuSample s;
    TEST_ASSERT_TRUE(mpu.readSample(s));
}

void test_at_rest_magnitude_is_about_1g() {
    ImuSample s;
    TEST_ASSERT_TRUE(mpu.readSample(s));
    const float ax = Mpu6050::toG(s.ax);
    const float ay = Mpu6050::toG(s.ay);
    const float az = Mpu6050::toG(s.az);
    const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
    Serial.printf("Rest magnitude: %.3f g\n", magnitude);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, magnitude);
}

// Prints ~1 s of samples for visual inspection (rotate/shake the board).
void test_print_samples() {
    const uint32_t start = millis();
    uint32_t count = 0;
    while (millis() - start < 1000) {
        ImuSample s;
        if (!mpu.readSample(s)) continue;
        count++;
        Serial.printf("a[g] %6.2f %6.2f %6.2f | g[dps] %8.1f %8.1f %8.1f\n",
                      Mpu6050::toG(s.ax), Mpu6050::toG(s.ay), Mpu6050::toG(s.az),
                      Mpu6050::toDps(s.gx), Mpu6050::toDps(s.gy), Mpu6050::toDps(s.gz));
        delay(10);  // ~100 Hz, matches the sensor's sample rate
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, count);
}

void setup() {
    delay(2000);  // Allow serial monitor to attach
    Serial.begin(115200);
    Wire.begin(AppConfig::kImuSdaPin, AppConfig::kImuSclPin, AppConfig::kImuI2cHz);

    UNITY_BEGIN();
    RUN_TEST(test_who_am_i);
    RUN_TEST(test_begin_applies_config);
    RUN_TEST(test_read_sample_succeeds);
    RUN_TEST(test_at_rest_magnitude_is_about_1g);
    RUN_TEST(test_print_samples);
    UNITY_END();
}

void loop() {}
