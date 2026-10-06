// Unit tests for Button debounce logic.
// Run: pio test -e esp32-s3-supermini-test -f test_button

#include <Arduino.h>
#include <unity.h>

#include "Input/Button.h"

namespace {
constexpr uint32_t kDebounceMs = 30;
} // namespace

void test_contact_bounce_inside_debounce_window_never_fires() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    // Rapid flicker during debounce window.
    TEST_ASSERT_FALSE(button.update(true, t));
    TEST_ASSERT_FALSE(button.update(false, t += 5));
    TEST_ASSERT_FALSE(button.update(true, t += 5));
    TEST_ASSERT_FALSE(button.update(false, t += 5));
    TEST_ASSERT_FALSE(button.update(false, t += 5));
}

void test_clean_press_fires_exactly_once() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    TEST_ASSERT_FALSE(button.update(true, t));
    t += 100;
    TEST_ASSERT_FALSE(button.update(true, t));

    // Level drops LOW and settles past debounce window.
    t += 1;
    TEST_ASSERT_FALSE(button.update(false, t));
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(false, t));

    t += 10;
    TEST_ASSERT_FALSE(button.update(false, t));
}

void test_press_shorter_than_debounce_never_fires() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    button.update(true, t);
    t += 5;
    button.update(false, t);
    t += 10;
    TEST_ASSERT_FALSE(button.update(true, t));
}

void test_release_then_repress_fires_a_second_time() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    button.update(true, t);
    t += kDebounceMs;
    button.update(false, t);
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(false, t));

    t += 1;
    button.update(true, t);
    t += kDebounceMs;
    TEST_ASSERT_FALSE(button.update(true, t));

    t += 1;
    button.update(false, t);
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(false, t));
}

void test_active_high_polarity() {
    Button button(kDebounceMs, /*activeLow=*/false);
    uint32_t t = 0;

    button.update(false, t);
    t += kDebounceMs;
    button.update(true, t);
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(true, t));
}

void setup() {
    delay(2000); // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_contact_bounce_inside_debounce_window_never_fires);
    RUN_TEST(test_clean_press_fires_exactly_once);
    RUN_TEST(test_press_shorter_than_debounce_never_fires);
    RUN_TEST(test_release_then_repress_fires_a_second_time);
    RUN_TEST(test_active_high_polarity);
    UNITY_END();
}

void loop() {}
