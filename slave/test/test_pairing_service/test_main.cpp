// Unit tests for slave PairingService.
// Run: pio test -e esp32-s3-supermini-test -f test_pairing_service

#include <Arduino.h>
#include <unity.h>

#include "Pairing/PairingService.h"

namespace {
constexpr uint32_t kTimeoutMs = 10000;

using State = PairingService::State;

int asInt(State state) { return static_cast<int>(state); }
}  // namespace

void test_start_searching_enters_searching_state() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));
    TEST_ASSERT_FALSE(service.isActive());

    service.startSearching();
    TEST_ASSERT_TRUE(service.isSearching());
    TEST_ASSERT_TRUE(service.isActive());
    TEST_ASSERT_FALSE(service.isWaiting());
}

void test_pair_wait_while_searching_moves_to_waiting() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    service.onPairWait();

    TEST_ASSERT_EQUAL(asInt(State::Waiting), asInt(service.getState()));
    TEST_ASSERT_TRUE(service.isWaiting());
    TEST_ASSERT_FALSE(service.isSearching());
    TEST_ASSERT_TRUE(service.isActive());
}

void test_pair_wait_is_a_no_op_when_not_pairing() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.onPairWait();  // Idle
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));

    service.startSearching();
    service.onPairAccepted();
    service.onPairWait();  // Paired
    TEST_ASSERT_EQUAL(asInt(State::Paired), asInt(service.getState()));
}

void test_pair_accepted_pairs_from_searching_or_waiting() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    // The master accepted at once (belt already registered).
    service.startSearching();
    service.onPairAccepted();
    TEST_ASSERT_EQUAL(asInt(State::Paired), asInt(service.getState()));

    // The usual path: PairWait first, PairAccept after the caregiver pressed Save.
    service.startSearching();
    service.onPairWait();
    service.onPairAccepted();
    TEST_ASSERT_EQUAL(asInt(State::Paired), asInt(service.getState()));
}

void test_pair_accepted_is_a_no_op_outside_an_attempt() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.onPairAccepted();  // Idle
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));

    service.startSearching();
    fakeNow = kTimeoutMs;
    service.tick();
    service.onPairAccepted();  // TimedOut
    TEST_ASSERT_EQUAL(asInt(State::TimedOut), asInt(service.getState()));
}

void test_resume_searching_returns_from_waiting_only() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    service.resumeSearching();  // already Searching: nothing changes
    TEST_ASSERT_TRUE(service.isSearching());

    service.onPairWait();
    service.resumeSearching();
    TEST_ASSERT_TRUE(service.isSearching());
}

void test_resume_searching_keeps_the_attempt_timeout_running() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    fakeNow = 4000;
    service.onPairWait();
    fakeNow = 6000;
    service.resumeSearching();

    TEST_ASSERT_EQUAL_UINT32(kTimeoutMs - 6000, service.remainingMs());
    fakeNow = kTimeoutMs;
    service.tick();
    TEST_ASSERT_EQUAL(asInt(State::TimedOut), asInt(service.getState()));
}

void test_tick_times_out_while_searching_and_while_waiting() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    fakeNow = kTimeoutMs - 1;
    service.tick();
    TEST_ASSERT_TRUE(service.isSearching());
    fakeNow = kTimeoutMs;
    service.tick();
    TEST_ASSERT_EQUAL(asInt(State::TimedOut), asInt(service.getState()));

    fakeNow = 20000;
    service.reset();
    service.startSearching();
    service.onPairWait();
    fakeNow = 20000 + kTimeoutMs;
    service.tick();
    TEST_ASSERT_EQUAL(asInt(State::TimedOut), asInt(service.getState()));
}

void test_reset_returns_to_idle_from_paired_or_timed_out() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    service.onPairAccepted();
    service.reset();
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));

    service.startSearching();
    fakeNow = kTimeoutMs;
    service.tick();
    service.reset();
    TEST_ASSERT_EQUAL(asInt(State::Idle), asInt(service.getState()));
}

void test_reset_does_not_interrupt_an_active_attempt() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.startSearching();
    service.onPairWait();
    service.reset();
    TEST_ASSERT_EQUAL(asInt(State::Waiting), asInt(service.getState()));
}

void test_remaining_ms_is_zero_outside_an_attempt() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    TEST_ASSERT_EQUAL_UINT32(0, service.remainingMs());

    service.startSearching();
    TEST_ASSERT_EQUAL_UINT32(kTimeoutMs, service.remainingMs());

    fakeNow = kTimeoutMs / 2;
    TEST_ASSERT_EQUAL_UINT32(kTimeoutMs / 2, service.remainingMs());

    service.onPairAccepted();
    TEST_ASSERT_EQUAL_UINT32(0, service.remainingMs());
}

void setup() {
    delay(2000);  // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_start_searching_enters_searching_state);
    RUN_TEST(test_pair_wait_while_searching_moves_to_waiting);
    RUN_TEST(test_pair_wait_is_a_no_op_when_not_pairing);
    RUN_TEST(test_pair_accepted_pairs_from_searching_or_waiting);
    RUN_TEST(test_pair_accepted_is_a_no_op_outside_an_attempt);
    RUN_TEST(test_resume_searching_returns_from_waiting_only);
    RUN_TEST(test_resume_searching_keeps_the_attempt_timeout_running);
    RUN_TEST(test_tick_times_out_while_searching_and_while_waiting);
    RUN_TEST(test_reset_returns_to_idle_from_paired_or_timed_out);
    RUN_TEST(test_reset_does_not_interrupt_an_active_attempt);
    RUN_TEST(test_remaining_ms_is_zero_outside_an_attempt);
    UNITY_END();
}

void loop() {}
