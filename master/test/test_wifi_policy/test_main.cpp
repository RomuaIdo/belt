// Unit tests for the Wi-Fi disconnect-reason policy.
// Run: pio test -e esp32-s3-devkitc-1-test -f test_wifi_policy

#include <Arduino.h>
#include <unity.h>

#include "Network/WifiPolicy.h"

void test_auth_fail_and_mic_failure_are_rejections() {
    TEST_ASSERT_TRUE(classifyWifiFailure(WIFI_REASON_AUTH_FAIL) == WifiFailure::AuthRejected);
    TEST_ASSERT_TRUE(classifyWifiFailure(WIFI_REASON_MIC_FAILURE) == WifiFailure::AuthRejected);
}

void test_handshake_timeouts_are_classified_apart() {
    TEST_ASSERT_TRUE(classifyWifiFailure(WIFI_REASON_HANDSHAKE_TIMEOUT) == WifiFailure::HandshakeTimeout);
    TEST_ASSERT_TRUE(classifyWifiFailure(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) == WifiFailure::HandshakeTimeout);
}

void test_generic_disconnects_are_other() {
    TEST_ASSERT_TRUE(classifyWifiFailure(WIFI_REASON_NO_AP_FOUND) == WifiFailure::Other);
    TEST_ASSERT_TRUE(classifyWifiFailure(WIFI_REASON_BEACON_TIMEOUT) == WifiFailure::Other);
    TEST_ASSERT_TRUE(classifyWifiFailure(WIFI_REASON_ASSOC_LEAVE) == WifiFailure::Other);
}

void test_rejection_means_wrong_password_at_once() {
    WifiFailureTracker tracker;
    TEST_ASSERT_TRUE(tracker.recordFailure(WIFI_REASON_AUTH_FAIL));
}

void test_one_handshake_timeout_is_not_a_wrong_password() {
    // E.g. the router rebooting with the right password saved.
    WifiFailureTracker tracker;
    TEST_ASSERT_FALSE(tracker.recordFailure(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
}

void test_repeated_handshake_timeouts_mean_wrong_password() {
    WifiFailureTracker tracker;
    for (uint8_t i = 1; i < WIFI_HANDSHAKE_TIMEOUT_LIMIT; ++i) {
        TEST_ASSERT_FALSE(tracker.recordFailure(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
    }
    TEST_ASSERT_TRUE(tracker.recordFailure(WIFI_REASON_HANDSHAKE_TIMEOUT));
    // Stays wrong on the next timeout too.
    TEST_ASSERT_TRUE(tracker.recordFailure(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
}

void test_other_failures_never_mean_wrong_password() {
    WifiFailureTracker tracker;
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_FALSE(tracker.recordFailure(WIFI_REASON_NO_AP_FOUND));
    }
    TEST_ASSERT_EQUAL_UINT8(0, tracker.handshakeTimeoutCount());
}

void test_reset_forgets_handshake_timeouts() {
    WifiFailureTracker tracker;
    for (uint8_t i = 1; i < WIFI_HANDSHAKE_TIMEOUT_LIMIT; ++i) {
        tracker.recordFailure(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);
    }
    tracker.reset();
    TEST_ASSERT_FALSE(tracker.recordFailure(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_auth_fail_and_mic_failure_are_rejections);
    RUN_TEST(test_handshake_timeouts_are_classified_apart);
    RUN_TEST(test_generic_disconnects_are_other);
    RUN_TEST(test_rejection_means_wrong_password_at_once);
    RUN_TEST(test_one_handshake_timeout_is_not_a_wrong_password);
    RUN_TEST(test_repeated_handshake_timeouts_mean_wrong_password);
    RUN_TEST(test_other_failures_never_mean_wrong_password);
    RUN_TEST(test_reset_forgets_handshake_timeouts);
    UNITY_END();
}

void loop() {}
