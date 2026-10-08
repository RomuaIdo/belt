// Unit tests for the status LED's breathing curve.
// Run: pio test -e esp32-s3-devkitc-1-test -f test_status_led

#include <Arduino.h>
#include <unity.h>

#include "Util/StatusLed.h"

void test_breathing_starts_dark() {
    TEST_ASSERT_EQUAL_UINT8(0, breathingLevel(0, 1000, 48));
}

void test_breathing_peaks_halfway() {
    TEST_ASSERT_EQUAL_UINT8(48, breathingLevel(500, 1000, 48));
}

void test_breathing_is_symmetric() {
    TEST_ASSERT_EQUAL_UINT8(breathingLevel(250, 1000, 48), breathingLevel(750, 1000, 48));
    TEST_ASSERT_EQUAL_UINT8(24, breathingLevel(250, 1000, 48));
}

void test_breathing_repeats_every_period() {
    TEST_ASSERT_EQUAL_UINT8(breathingLevel(300, 1000, 48), breathingLevel(5300, 1000, 48));
    TEST_ASSERT_EQUAL_UINT8(0, breathingLevel(7000, 1000, 48));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_breathing_starts_dark);
    RUN_TEST(test_breathing_peaks_halfway);
    RUN_TEST(test_breathing_is_symmetric);
    RUN_TEST(test_breathing_repeats_every_period);
    UNITY_END();
}

void loop() {}
