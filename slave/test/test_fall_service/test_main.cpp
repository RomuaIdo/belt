// Unit tests for FallService (countdown after a confirmed fall).
// Run: pio test -e esp32-s3-supermini-test -f test_fall_service

#include <Arduino.h>
#include <unity.h>

#include "Fall/FallService.h"

namespace {
constexpr uint32_t kWindowMs = 10000;
uint32_t fakeNow = 0;

FallService makeService() {
    fakeNow = 0;
    return FallService(kWindowMs, []() { return fakeNow; });
}
}  // namespace

void test_starts_idle_and_update_does_nothing() {
    FallService service = makeService();
    TEST_ASSERT_TRUE(service.getState() == FallService::State::Idle);
    fakeNow = 60000;
    TEST_ASSERT_FALSE(service.update());
    TEST_ASSERT_FALSE(service.cancel());  // nothing to cancel
}

void test_countdown_fires_once_when_it_ends() {
    FallService service = makeService();
    fakeNow = 1000;
    service.onFallDetected();
    TEST_ASSERT_TRUE(service.isCountingDown());

    fakeNow = 1000 + kWindowMs - 1;
    TEST_ASSERT_FALSE(service.update());
    TEST_ASSERT_TRUE(service.isCountingDown());

    fakeNow = 1000 + kWindowMs;
    TEST_ASSERT_TRUE(service.update());
    TEST_ASSERT_TRUE(service.getState() == FallService::State::Idle);
    TEST_ASSERT_FALSE(service.update());  // only once
}

void test_cancel_stops_the_countdown() {
    FallService service = makeService();
    service.onFallDetected();
    fakeNow = 3000;
    TEST_ASSERT_TRUE(service.cancel());
    TEST_ASSERT_TRUE(service.getState() == FallService::State::Idle);

    fakeNow = 3000 + kWindowMs;
    TEST_ASSERT_FALSE(service.update());  // nothing is sent
}

void test_second_fall_does_not_restart_the_countdown() {
    FallService service = makeService();
    service.onFallDetected();  // at 0
    fakeNow = 5000;
    service.onFallDetected();  // ignored
    fakeNow = kWindowMs;
    TEST_ASSERT_TRUE(service.update());
}

void test_survives_millis_wraparound() {
    FallService service = makeService();
    fakeNow = 0xFFFFFFFFu - 1000;
    service.onFallDetected();
    fakeNow = kWindowMs - 1002;  // wrapped, 1 ms short
    TEST_ASSERT_FALSE(service.update());
    fakeNow = kWindowMs - 1001;
    TEST_ASSERT_TRUE(service.update());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_starts_idle_and_update_does_nothing);
    RUN_TEST(test_countdown_fires_once_when_it_ends);
    RUN_TEST(test_cancel_stops_the_countdown);
    RUN_TEST(test_second_fall_does_not_restart_the_countdown);
    RUN_TEST(test_survives_millis_wraparound);
    UNITY_END();
}

void loop() {}
