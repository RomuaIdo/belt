// Unit tests for slave AlertService.
// Run: pio test -e esp32-s3-supermini-test -f test_alert_service

#include <Arduino.h>
#include <unity.h>

#include "Alert/AlertService.h"

namespace {
constexpr uint32_t kHoldMs = 2000;

using State = AlertService::State;

int asInt(State state) { return static_cast<int>(state); }
}  // namespace

void test_starts_idle_and_ignores_a_level_without_a_press_edge() {
    uint32_t fakeNow = 0;
    AlertService service(kHoldMs, [&]() { return fakeNow; });

    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));
    fakeNow = 5000;
    TEST_ASSERT_FALSE(service.update(true));  // e.g. button already held at boot
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));
}

void test_press_enters_holding() {
    uint32_t fakeNow = 0;
    AlertService service(kHoldMs, [&]() { return fakeNow; });

    service.onPress();
    TEST_ASSERT_EQUAL(asInt(State::Holding), asInt(service.getState()));
    TEST_ASSERT_FALSE(service.update(true));
}

void test_hold_reached_fires_once_then_sending() {
    uint32_t fakeNow = 100;
    AlertService service(kHoldMs, [&]() { return fakeNow; });

    service.onPress();
    fakeNow = 100 + kHoldMs - 1;
    TEST_ASSERT_FALSE(service.update(true));

    fakeNow = 100 + kHoldMs;
    TEST_ASSERT_TRUE(service.update(true));
    TEST_ASSERT_EQUAL(asInt(State::Sending), asInt(service.getState()));

    fakeNow += 5000;
    TEST_ASSERT_FALSE(service.update(true));  // one alert per hold
    TEST_ASSERT_EQUAL(asInt(State::Sending), asInt(service.getState()));
}

void test_release_before_hold_cancels() {
    uint32_t fakeNow = 0;
    AlertService service(kHoldMs, [&]() { return fakeNow; });

    service.onPress();
    fakeNow = 500;
    TEST_ASSERT_FALSE(service.update(false));
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));

    fakeNow = 10000;
    TEST_ASSERT_FALSE(service.update(true));  // no new press edge: nothing fires
}

void test_release_after_sending_returns_to_idle() {
    uint32_t fakeNow = 0;
    AlertService service(kHoldMs, [&]() { return fakeNow; });

    service.onPress();
    fakeNow = kHoldMs;
    TEST_ASSERT_TRUE(service.update(true));
    TEST_ASSERT_FALSE(service.update(false));
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));
}

void test_new_press_after_release_can_fire_again() {
    uint32_t fakeNow = 0;
    AlertService service(kHoldMs, [&]() { return fakeNow; });

    service.onPress();
    fakeNow = kHoldMs;
    TEST_ASSERT_TRUE(service.update(true));
    service.update(false);

    fakeNow = 10000;
    service.onPress();
    fakeNow = 10000 + kHoldMs;
    TEST_ASSERT_TRUE(service.update(true));
}

void test_abort_goes_idle_until_next_press() {
    uint32_t fakeNow = 0;
    AlertService service(kHoldMs, [&]() { return fakeNow; });

    service.onPress();
    fakeNow = kHoldMs;
    TEST_ASSERT_TRUE(service.update(true));

    service.abort();
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));
    TEST_ASSERT_FALSE(service.update(true));  // still held, but no new press
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_starts_idle_and_ignores_a_level_without_a_press_edge);
    RUN_TEST(test_press_enters_holding);
    RUN_TEST(test_hold_reached_fires_once_then_sending);
    RUN_TEST(test_release_before_hold_cancels);
    RUN_TEST(test_release_after_sending_returns_to_idle);
    RUN_TEST(test_new_press_after_release_can_fire_again);
    RUN_TEST(test_abort_goes_idle_until_next_press);
    UNITY_END();
}

void loop() {}
