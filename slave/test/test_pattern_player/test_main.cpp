// Unit tests for the output PatternPlayer.
// Run: pio test -e esp32-s3-supermini-test -f test_pattern_player

#include <Arduino.h>
#include <unity.h>

#include "State/PatternPlayer.h"

void test_off_is_never_on() {
    PatternPlayer player;
    player.start(Pattern::off(), 100);
    TEST_ASSERT_FALSE(player.isOn(100));
    TEST_ASSERT_FALSE(player.isOn(5000));
    TEST_ASSERT_FALSE(player.finished(5000));
}

void test_steady_is_always_on_and_never_finishes() {
    PatternPlayer player;
    player.start(Pattern::steady(), 100);
    TEST_ASSERT_TRUE(player.isOn(100));
    TEST_ASSERT_TRUE(player.isOn(999999));
    TEST_ASSERT_FALSE(player.finished(999999));
}

void test_blink_alternates_on_and_off() {
    PatternPlayer player;
    player.start(Pattern::blink(100, 200), 1000);

    TEST_ASSERT_TRUE(player.isOn(1000));
    TEST_ASSERT_TRUE(player.isOn(1099));
    TEST_ASSERT_FALSE(player.isOn(1100));
    TEST_ASSERT_FALSE(player.isOn(1299));
    TEST_ASSERT_TRUE(player.isOn(1300));  // second cycle
}

void test_endless_blink_never_finishes() {
    PatternPlayer player;
    player.start(Pattern::blink(100, 100), 0);
    TEST_ASSERT_FALSE(player.finished(1000000));
    TEST_ASSERT_TRUE(player.isOn(1000000));  // 1000000 % 200 = 0
}

void test_finite_blink_stops_after_its_repeats() {
    PatternPlayer player;
    player.start(Pattern::blink(80, 80, 2), 500);

    TEST_ASSERT_FALSE(player.finished(500));
    TEST_ASSERT_TRUE(player.isOn(500));
    TEST_ASSERT_TRUE(player.isOn(660));  // second pulse
    TEST_ASSERT_FALSE(player.finished(819));
    TEST_ASSERT_TRUE(player.finished(820));  // 2 * 160 ms after the start
    TEST_ASSERT_FALSE(player.isOn(820));
    TEST_ASSERT_FALSE(player.isOn(9999));
}

void test_blink_without_off_time_is_one_solid_pulse() {
    PatternPlayer player;
    player.start(Pattern::blink(400, 0, 1), 0);

    TEST_ASSERT_TRUE(player.isOn(399));
    TEST_ASSERT_TRUE(player.finished(400));
    TEST_ASSERT_FALSE(player.isOn(400));
}

void test_start_restarts_the_pattern() {
    PatternPlayer player;
    player.start(Pattern::blink(100, 100, 1), 0);
    TEST_ASSERT_TRUE(player.finished(200));

    player.start(Pattern::blink(100, 100, 1), 1000);
    TEST_ASSERT_FALSE(player.finished(1000));
    TEST_ASSERT_TRUE(player.isOn(1050));
}

void test_clock_rollover_is_handled() {
    PatternPlayer player;
    player.start(Pattern::blink(100, 100, 1), 0xFFFFFFF0u);

    TEST_ASSERT_TRUE(player.isOn(0xFFFFFFF0u));
    TEST_ASSERT_FALSE(player.finished(0x00000010u));   // 32 ms later
    TEST_ASSERT_TRUE(player.finished(0x000000D0u));    // 224 ms later
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_off_is_never_on);
    RUN_TEST(test_steady_is_always_on_and_never_finishes);
    RUN_TEST(test_blink_alternates_on_and_off);
    RUN_TEST(test_endless_blink_never_finishes);
    RUN_TEST(test_finite_blink_stops_after_its_repeats);
    RUN_TEST(test_blink_without_off_time_is_one_solid_pulse);
    RUN_TEST(test_start_restarts_the_pattern);
    RUN_TEST(test_clock_rollover_is_handled);
    UNITY_END();
}

void loop() {}
