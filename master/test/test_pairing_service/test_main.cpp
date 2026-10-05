// Unit tests for master PairingService.
// Run: pio test -e esp32-s3-devkitc-1-test -f test_pairing_service

#include <Arduino.h>
#include <unity.h>

#include "Network/PairingService.h"

namespace {
constexpr uint32_t kTimeoutMs = 10000;
constexpr const char* kTargetMac = "AA:BB:CC:DD:EE:01";
constexpr const char* kOtherMac = "AA:BB:CC:DD:EE:02";
} // namespace

void test_start_becomes_active_and_pending_for_the_target_mac() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    TEST_ASSERT_FALSE(service.isActive());

    service.start(kTargetMac);
    TEST_ASSERT_TRUE(service.isActive());
    TEST_ASSERT_TRUE(service.isPendingFor(kTargetMac));
    TEST_ASSERT_FALSE(service.isPendingFor(kOtherMac));
    TEST_ASSERT_EQUAL_STRING(kTargetMac, service.getTargetMac().c_str());
}

void test_is_pending_for_is_case_insensitive() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.start("aa:bb:cc:dd:ee:01");
    TEST_ASSERT_TRUE(service.isPendingFor(kTargetMac));
}

void test_tick_times_out_after_confirm_timeout() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.start(kTargetMac);

    fakeNow = kTimeoutMs - 1;
    service.tick();
    TEST_ASSERT_TRUE(service.isActive());

    fakeNow = kTimeoutMs;
    service.tick();
    TEST_ASSERT_FALSE(service.isActive());
}

void test_confirm_deactivates_immediately() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.start(kTargetMac);
    service.confirm();

    TEST_ASSERT_FALSE(service.isActive());
    TEST_ASSERT_FALSE(service.isPendingFor(kTargetMac));
}

void test_stop_deactivates_immediately() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.start(kTargetMac);
    service.stop();

    TEST_ASSERT_FALSE(service.isActive());
}

void test_starting_again_with_a_different_mac_replaces_the_pending_target() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.start(kTargetMac);
    service.start(kOtherMac);

    TEST_ASSERT_FALSE(service.isPendingFor(kTargetMac));
    TEST_ASSERT_TRUE(service.isPendingFor(kOtherMac));
}

void test_remaining_ms_counts_down_to_zero() {
    uint32_t fakeNow = 0;
    PairingService service(kTimeoutMs, [&]() { return fakeNow; });

    service.start(kTargetMac);
    TEST_ASSERT_EQUAL_UINT32(kTimeoutMs, service.remainingMs());

    fakeNow = kTimeoutMs / 2;
    TEST_ASSERT_EQUAL_UINT32(kTimeoutMs / 2, service.remainingMs());

    service.stop();
    TEST_ASSERT_EQUAL_UINT32(0, service.remainingMs());
}

void setup() {
    delay(2000); // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_start_becomes_active_and_pending_for_the_target_mac);
    RUN_TEST(test_is_pending_for_is_case_insensitive);
    RUN_TEST(test_tick_times_out_after_confirm_timeout);
    RUN_TEST(test_confirm_deactivates_immediately);
    RUN_TEST(test_stop_deactivates_immediately);
    RUN_TEST(test_starting_again_with_a_different_mac_replaces_the_pending_target);
    RUN_TEST(test_remaining_ms_counts_down_to_zero);
    UNITY_END();
}

void loop() {}
