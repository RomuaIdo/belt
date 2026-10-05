// Unit tests for Button's debounce logic (synthetic rawLevel/nowMs sequences, no GPIO).
// We intend to implement a hardware debounce circuit. But this is a fallback.
// Run with: pio test -e esp32-s3-supermini-test -f test_button

#include <Arduino.h>
#include <unity.h>

#include "Input/Button.h"

namespace {
constexpr uint32_t kDebounceMs = 30;
} // namespace

void test_contact_bounce_inside_debounce_window_never_fires() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    // Idle (HIGH=true) -> flicker rapidly within the debounce window.
    TEST_ASSERT_FALSE(button.update(true, t));
    TEST_ASSERT_FALSE(button.update(false, t += 5));  // bounce
    TEST_ASSERT_FALSE(button.update(true, t += 5));   // bounce
    TEST_ASSERT_FALSE(button.update(false, t += 5));  // bounce, still well under 30ms settle
    TEST_ASSERT_FALSE(button.update(false, t += 5));  // still settling (only 5ms since last edge)
}

void test_clean_press_fires_exactly_once() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    TEST_ASSERT_FALSE(button.update(true, t)); // idle, high
    t += 100;
    TEST_ASSERT_FALSE(button.update(true, t)); // still idle, already stable: no edge

    // Press: level drops low and stays there past the debounce window.
    t += 1;
    TEST_ASSERT_FALSE(button.update(false, t)); // edge just happened, not yet settled
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(false, t)); // settled low -> fires once

    t += 10;
    TEST_ASSERT_FALSE(button.update(false, t)); // still held, no repeat firing
}

void test_press_shorter_than_debounce_never_fires() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    button.update(true, t);
    t += 5;
    button.update(false, t); // brief dip
    t += 10;                  // only 10ms elapsed, under the 30ms debounce
    TEST_ASSERT_FALSE(button.update(true, t)); // bounced back to idle before settling
}

void test_release_then_repress_fires_a_second_time() {
    Button button(kDebounceMs, /*activeLow=*/true);
    uint32_t t = 0;

    button.update(true, t); // idle
    t += kDebounceMs;
    button.update(false, t);
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(false, t)); // first press fires

    t += 1;
    button.update(true, t); // release
    t += kDebounceMs;
    TEST_ASSERT_FALSE(button.update(true, t)); // release settling, not a press

    t += 1;
    button.update(false, t); // second press
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(false, t)); // fires again
}

void test_active_high_polarity() {
    Button button(kDebounceMs, /*activeLow=*/false);
    uint32_t t = 0;

    button.update(false, t); // idle is LOW for an active-high button
    t += kDebounceMs;
    button.update(true, t); // press: level goes HIGH
    t += kDebounceMs;
    TEST_ASSERT_TRUE(button.update(true, t));
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_contact_bounce_inside_debounce_window_never_fires);
    RUN_TEST(test_clean_press_fires_exactly_once);
    RUN_TEST(test_press_shorter_than_debounce_never_fires);
    RUN_TEST(test_release_then_repress_fires_a_second_time);
    RUN_TEST(test_active_high_polarity);
    UNITY_END();
}

void loop() {}
