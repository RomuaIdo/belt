// Unit tests for slave's PairingService listening-window state machine (fake clock, no hardware).
// Run with: pio test -e esp32-s3-supermini-test -f test_pairing_service

#include <Arduino.h>
#include <unity.h>

#include "Pairing/PairingService.h"

namespace {
constexpr uint32_t kListenWindowMs = 10000;
} // namespace

void test_start_listening_enters_listening_state() {
    uint32_t fakeNow = 0;
    PairingService service(kListenWindowMs, [&]() { return fakeNow; });

    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));

    service.startListening();
    TEST_ASSERT_TRUE(service.isListening());
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Listening), static_cast<int>(service.getState()));
}

void test_tick_times_out_after_the_listen_window() {
    uint32_t fakeNow = 0;
    PairingService service(kListenWindowMs, [&]() { return fakeNow; });

    service.startListening();

    fakeNow = kListenWindowMs - 1;
    service.tick();
    TEST_ASSERT_TRUE(service.isListening());

    fakeNow = kListenWindowMs;
    service.tick();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::TimedOut), static_cast<int>(service.getState()));
    TEST_ASSERT_FALSE(service.isListening());
}

void test_on_pair_request_received_while_listening_transitions_to_paired() {
    uint32_t fakeNow = 0;
    PairingService service(kListenWindowMs, [&]() { return fakeNow; });

    service.startListening();
    service.onPairRequestReceived();

    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Paired), static_cast<int>(service.getState()));
}

void test_on_pair_request_received_is_a_no_op_outside_listening() {
    uint32_t fakeNow = 0;
    PairingService service(kListenWindowMs, [&]() { return fakeNow; });

    // Idle: never started listening.
    service.onPairRequestReceived();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));

    // TimedOut: already expired.
    service.startListening();
    fakeNow = kListenWindowMs;
    service.tick();
    service.onPairRequestReceived();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::TimedOut), static_cast<int>(service.getState()));
}

void test_reset_returns_to_idle_from_paired_or_timed_out() {
    uint32_t fakeNow = 0;
    PairingService service(kListenWindowMs, [&]() { return fakeNow; });

    service.startListening();
    service.onPairRequestReceived();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Paired), static_cast<int>(service.getState()));
    service.reset();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));

    service.startListening();
    fakeNow = kListenWindowMs;
    service.tick();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::TimedOut), static_cast<int>(service.getState()));
    service.reset();
    TEST_ASSERT_EQUAL(static_cast<int>(PairingService::State::Idle), static_cast<int>(service.getState()));
}

void test_remaining_ms_is_zero_outside_listening() {
    uint32_t fakeNow = 0;
    PairingService service(kListenWindowMs, [&]() { return fakeNow; });

    TEST_ASSERT_EQUAL_UINT32(0, service.remainingMs());

    service.startListening();
    TEST_ASSERT_EQUAL_UINT32(kListenWindowMs, service.remainingMs());

    fakeNow = kListenWindowMs / 2;
    TEST_ASSERT_EQUAL_UINT32(kListenWindowMs / 2, service.remainingMs());
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_start_listening_enters_listening_state);
    RUN_TEST(test_tick_times_out_after_the_listen_window);
    RUN_TEST(test_on_pair_request_received_while_listening_transitions_to_paired);
    RUN_TEST(test_on_pair_request_received_is_a_no_op_outside_listening);
    RUN_TEST(test_reset_returns_to_idle_from_paired_or_timed_out);
    RUN_TEST(test_remaining_ms_is_zero_outside_listening);
    UNITY_END();
}

void loop() {}
