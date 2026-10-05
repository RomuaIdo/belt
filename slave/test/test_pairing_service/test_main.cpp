// Unit tests for slave's PairingService broadcast-search state machine (fake clock, no hardware).
// Run with: pio test -e esp32-s3-supermini-test -f test_pairing_service

#include <Arduino.h>
#include <unity.h>

#include "Pairing/PairingService.h"

namespace {
constexpr uint32_t kSearchTimeoutMs = 10000;
} // namespace

void test_start_searching_enters_searching_state() {
    uint32_t fakeNow = 0;
    PairingService service(kSearchTimeoutMs, [&]() { return fakeNow; });

    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));

    service.startSearching();
    TEST_ASSERT_TRUE(service.isSearching());
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Searching), static_cast<int>(service.getState()));
}

void test_tick_times_out_after_the_search_timeout() {
    uint32_t fakeNow = 0;
    PairingService service(kSearchTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();

    fakeNow = kSearchTimeoutMs - 1;
    service.tick();
    TEST_ASSERT_TRUE(service.isSearching());

    fakeNow = kSearchTimeoutMs;
    service.tick();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::TimedOut), static_cast<int>(service.getState()));
    TEST_ASSERT_FALSE(service.isSearching());
}

void test_on_pair_response_received_while_searching_transitions_to_paired() {
    uint32_t fakeNow = 0;
    PairingService service(kSearchTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    service.onPairResponseReceived();

    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Paired), static_cast<int>(service.getState()));
}

void test_on_pair_response_received_is_a_no_op_outside_searching() {
    uint32_t fakeNow = 0;
    PairingService service(kSearchTimeoutMs, [&]() { return fakeNow; });

    // Idle: never started searching.
    service.onPairResponseReceived();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));

    // TimedOut: already expired.
    service.startSearching();
    fakeNow = kSearchTimeoutMs;
    service.tick();
    service.onPairResponseReceived();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::TimedOut), static_cast<int>(service.getState()));
}

void test_reset_returns_to_idle_from_paired_or_timed_out() {
    uint32_t fakeNow = 0;
    PairingService service(kSearchTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    service.onPairResponseReceived();
    service.reset();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));

    service.startSearching();
    fakeNow = kSearchTimeoutMs;
    service.tick();
    service.reset();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));
}

void test_remaining_ms_is_zero_outside_searching() {
    uint32_t fakeNow = 0;
    PairingService service(kSearchTimeoutMs, [&]() { return fakeNow; });

    TEST_ASSERT_EQUAL_UINT32(0, service.remainingMs());

    service.startSearching();
    TEST_ASSERT_EQUAL_UINT32(kSearchTimeoutMs, service.remainingMs());

    fakeNow = kSearchTimeoutMs / 2;
    TEST_ASSERT_EQUAL_UINT32(kSearchTimeoutMs / 2, service.remainingMs());
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_start_searching_enters_searching_state);
    RUN_TEST(test_tick_times_out_after_the_search_timeout);
    RUN_TEST(test_on_pair_response_received_while_searching_transitions_to_paired);
    RUN_TEST(test_on_pair_response_received_is_a_no_op_outside_searching);
    RUN_TEST(test_reset_returns_to_idle_from_paired_or_timed_out);
    RUN_TEST(test_remaining_ms_is_zero_outside_searching);
    UNITY_END();
}

void loop() {}
