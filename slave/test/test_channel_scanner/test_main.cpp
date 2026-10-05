// Unit tests for ChannelScanner's pure channel-cycling timing (fake clock, no radio).
// Run with: pio test -e esp32-s3-supermini-test -f test_channel_scanner

#include <Arduino.h>
#include <unity.h>

#include "Pairing/ChannelScanner.h"

namespace {
constexpr uint8_t kMinChannel = 1;
constexpr uint8_t kMaxChannel = 13;
constexpr uint32_t kDwellMs = 300;
} // namespace

void test_reset_starts_at_min_channel() {
    uint32_t fakeNow = 1234;
    ChannelScanner scanner(kMinChannel, kMaxChannel, kDwellMs, [&]() { return fakeNow; });

    scanner.reset();
    TEST_ASSERT_EQUAL_UINT8(kMinChannel, scanner.currentChannel());
}

void test_tick_does_not_advance_before_dwell_elapses() {
    uint32_t fakeNow = 0;
    ChannelScanner scanner(kMinChannel, kMaxChannel, kDwellMs, [&]() { return fakeNow; });

    scanner.reset();
    fakeNow = kDwellMs - 1;
    TEST_ASSERT_FALSE(scanner.tick());
    TEST_ASSERT_EQUAL_UINT8(kMinChannel, scanner.currentChannel());
}

void test_tick_advances_exactly_once_per_dwell_period() {
    uint32_t fakeNow = 0;
    ChannelScanner scanner(kMinChannel, kMaxChannel, kDwellMs, [&]() { return fakeNow; });

    scanner.reset();
    fakeNow = kDwellMs;
    TEST_ASSERT_TRUE(scanner.tick());
    TEST_ASSERT_EQUAL_UINT8(kMinChannel + 1, scanner.currentChannel());

    // No further advance until another full dwell period passes.
    fakeNow += kDwellMs - 1;
    TEST_ASSERT_FALSE(scanner.tick());
}

void test_wraps_from_max_channel_back_to_min() {
    uint32_t fakeNow = 0;
    ChannelScanner scanner(kMinChannel, kMaxChannel, kDwellMs, [&]() { return fakeNow; });

    scanner.reset();
    for (uint8_t ch = kMinChannel; ch < kMaxChannel; ++ch) {
        fakeNow += kDwellMs;
        TEST_ASSERT_TRUE(scanner.tick());
    }
    TEST_ASSERT_EQUAL_UINT8(kMaxChannel, scanner.currentChannel());

    fakeNow += kDwellMs;
    TEST_ASSERT_TRUE(scanner.tick());
    TEST_ASSERT_EQUAL_UINT8(kMinChannel, scanner.currentChannel());
}

void test_reset_restarts_the_dwell_timer() {
    uint32_t fakeNow = 0;
    ChannelScanner scanner(kMinChannel, kMaxChannel, kDwellMs, [&]() { return fakeNow; });

    scanner.reset();
    fakeNow = kDwellMs - 1; // about to advance
    scanner.reset();        // restart: timer resets too
    TEST_ASSERT_FALSE(scanner.tick());
    TEST_ASSERT_EQUAL_UINT8(kMinChannel, scanner.currentChannel());
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_reset_starts_at_min_channel);
    RUN_TEST(test_tick_does_not_advance_before_dwell_elapses);
    RUN_TEST(test_tick_advances_exactly_once_per_dwell_period);
    RUN_TEST(test_wraps_from_max_channel_back_to_min);
    RUN_TEST(test_reset_restarts_the_dwell_timer);
    UNITY_END();
}

void loop() {}
