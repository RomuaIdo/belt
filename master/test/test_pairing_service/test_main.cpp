// Unit tests for master's PairingService broadcast-window state machine (fake clock, no hardware).
// Run with: pio test -e esp32-s3-devkitc-1-test -f test_pairing_service

#include <Arduino.h>
#include <unity.h>

#include "Network/PairingService.h"

namespace {

constexpr uint32_t kWindowMs = 10000;
constexpr uint32_t kIntervalMs = 2000;

} // namespace

void test_start_fires_one_beacon_immediately_on_next_tick() {
    uint32_t fakeNow = 1000;
    int beaconCount = 0;

    PairingService service(kWindowMs, kIntervalMs,
                            [&]() { ++beaconCount; },
                            [&]() { return fakeNow; });

    service.start();
    TEST_ASSERT_EQUAL_INT(0, beaconCount); // start() itself sends nothing

    service.tick();
    TEST_ASSERT_EQUAL_INT(1, beaconCount); // first tick() fires immediately
}

void test_beacons_repeat_every_interval() {
    uint32_t fakeNow = 0;
    int beaconCount = 0;

    PairingService service(kWindowMs, kIntervalMs,
                            [&]() { ++beaconCount; },
                            [&]() { return fakeNow; });

    service.start();
    service.tick(); // immediate first beacon
    TEST_ASSERT_EQUAL_INT(1, beaconCount);

    fakeNow += kIntervalMs / 2;
    service.tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, beaconCount, "must not fire before a full interval elapses");

    fakeNow += kIntervalMs; // now 1.5 intervals since the last beacon
    service.tick();
    TEST_ASSERT_EQUAL_INT(2, beaconCount);
}

void test_is_active_becomes_false_once_window_elapses() {
    uint32_t fakeNow = 0;
    PairingService service(kWindowMs, kIntervalMs, []() {}, [&]() { return fakeNow; });

    service.start();
    TEST_ASSERT_TRUE(service.isActive());

    fakeNow = kWindowMs - 1;
    service.tick();
    TEST_ASSERT_TRUE(service.isActive());

    fakeNow = kWindowMs;
    service.tick();
    TEST_ASSERT_FALSE(service.isActive());
}

void test_stop_forces_inactive_immediately() {
    uint32_t fakeNow = 0;
    PairingService service(kWindowMs, kIntervalMs, []() {}, [&]() { return fakeNow; });

    service.start();
    TEST_ASSERT_TRUE(service.isActive());

    service.stop();
    TEST_ASSERT_FALSE(service.isActive());
}

void test_restarting_while_active_resets_the_window() {
    uint32_t fakeNow = 0;
    PairingService service(kWindowMs, kIntervalMs, []() {}, [&]() { return fakeNow; });

    service.start();
    fakeNow = kWindowMs - 1; // about to expire
    service.tick();
    TEST_ASSERT_TRUE(service.isActive());

    service.start(); // restart resets the window
    service.tick();
    TEST_ASSERT_TRUE(service.isActive());
    TEST_ASSERT_EQUAL_UINT32(kWindowMs, service.remainingMs());
}

void test_remaining_ms_counts_down_to_zero() {
    uint32_t fakeNow = 0;
    PairingService service(kWindowMs, kIntervalMs, []() {}, [&]() { return fakeNow; });

    service.start();
    TEST_ASSERT_EQUAL_UINT32(kWindowMs, service.remainingMs());

    fakeNow = kWindowMs / 2;
    TEST_ASSERT_EQUAL_UINT32(kWindowMs / 2, service.remainingMs());

    service.stop();
    TEST_ASSERT_EQUAL_UINT32(0, service.remainingMs());
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_start_fires_one_beacon_immediately_on_next_tick);
    RUN_TEST(test_beacons_repeat_every_interval);
    RUN_TEST(test_is_active_becomes_false_once_window_elapses);
    RUN_TEST(test_stop_forces_inactive_immediately);
    RUN_TEST(test_restarting_while_active_resets_the_window);
    RUN_TEST(test_remaining_ms_counts_down_to_zero);
    UNITY_END();
}

void loop() {}
