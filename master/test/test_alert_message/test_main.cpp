// Run with: pio test -e esp32-s3-devkitc-1-test -f test_alert_message
// These tests need no Wi-Fi, credentials, or flash access.

#include <Arduino.h>
#include <unity.h>

#include "Messaging/alert_message.h"

namespace {
// 2026-09-09 14:32:00 UTC; with TZ=UTC0 that's 14:32 on 09/09/2026.
constexpr time_t kEventTime = 1788964320;

void useUtc() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
}

void test_empty_template_uses_default_message() {
    useUtc();
    const String out = formatAlertMessage("", "Maria", kEventTime, true);
    TEST_ASSERT_EQUAL_STRING("ALERT: Maria may have fallen. Detected at 14:32 on 09/09/2026.",
                             out.c_str());
}

void test_all_placeholders_are_replaced() {
    useUtc();
    const String out = formatAlertMessage("{nome}|{apelido}|{hora}|{data}",
                                          "Maria", kEventTime, true);
    TEST_ASSERT_EQUAL_STRING("Maria|Maria|14:32|09/09/2026", out.c_str());
}

void test_repeated_placeholders_are_all_replaced() {
    useUtc();
    const String out = formatAlertMessage("{nome} {nome}", "Ana", kEventTime, true);
    TEST_ASSERT_EQUAL_STRING("Ana Ana", out.c_str());
}

void test_invalid_clock_uses_dashes_for_time_and_date() {
    const String out = formatAlertMessage("{hora} {data}", "Maria", 0, false);
    TEST_ASSERT_EQUAL_STRING("--:-- --/--/----", out.c_str());
}

void test_unknown_placeholder_is_left_untouched() {
    useUtc();
    const String out = formatAlertMessage("Oi {nome} {foo}", "Maria", kEventTime, true);
    TEST_ASSERT_EQUAL_STRING("Oi Maria {foo}", out.c_str());
}

void test_empty_display_name_does_not_break() {
    useUtc();
    const String out = formatAlertMessage("[{nome}]", "", kEventTime, true);
    TEST_ASSERT_EQUAL_STRING("[]", out.c_str());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_empty_template_uses_default_message);
    RUN_TEST(test_all_placeholders_are_replaced);
    RUN_TEST(test_repeated_placeholders_are_all_replaced);
    RUN_TEST(test_invalid_clock_uses_dashes_for_time_and_date);
    RUN_TEST(test_unknown_placeholder_is_left_untouched);
    RUN_TEST(test_empty_display_name_does_not_break);
    UNITY_END();
}

void loop() {}
