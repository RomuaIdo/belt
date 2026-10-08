// Unit tests for the Wi-Fi disconnect-reason policy.
// Run: pio test -e esp32-s3-devkitc-1-test -f test_wifi_policy

#include <Arduino.h>
#include <unity.h>

#include "Network/WifiPolicy.h"

void test_auth_fail_is_a_password_failure() {
    TEST_ASSERT_TRUE(isWifiAuthFailure(WIFI_REASON_AUTH_FAIL));
}

void test_handshake_timeouts_are_password_failures() {
    TEST_ASSERT_TRUE(isWifiAuthFailure(WIFI_REASON_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_TRUE(isWifiAuthFailure(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_TRUE(isWifiAuthFailure(WIFI_REASON_MIC_FAILURE));
}

void test_network_not_found_is_not_a_password_failure() {
    // Transient network absence is not an auth failure.
    TEST_ASSERT_FALSE(isWifiAuthFailure(WIFI_REASON_NO_AP_FOUND));
}

void test_generic_disconnects_are_not_password_failures() {
    TEST_ASSERT_FALSE(isWifiAuthFailure(WIFI_REASON_BEACON_TIMEOUT));
    TEST_ASSERT_FALSE(isWifiAuthFailure(WIFI_REASON_ASSOC_LEAVE));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_auth_fail_is_a_password_failure);
    RUN_TEST(test_handshake_timeouts_are_password_failures);
    RUN_TEST(test_network_not_found_is_not_a_password_failure);
    RUN_TEST(test_generic_disconnects_are_not_password_failures);
    UNITY_END();
}

void loop() {}
