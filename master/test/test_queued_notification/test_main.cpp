// Unit tests for the pure in-memory QueuedNotification state object.
// No flash/LittleFS access here — see test_call_queue_storage for persistence and the power-loss simulation.
// Run with: pio test -e esp32-s3-devkitc-1-test -f test_queued_notification

#include <Arduino.h>
#include <unity.h>
#include <vector>

#include "Domain/QueuedNotification.h"

namespace {

QueuedNotification makeNotification() {
    std::vector<String> phones = {"11111111111", "22222222222", "33333333333"};
    return QueuedNotification("evt123", 1700000000, "AA:BB:CC:DD:EE:FF", "Fall alert", phones);
}

} // namespace

void test_constructor_stores_all_fields() {
    QueuedNotification n = makeNotification();

    TEST_ASSERT_EQUAL_STRING("evt123", n.getEventId().c_str());
    TEST_ASSERT_EQUAL_UINT32(1700000000, n.getTimestamp());
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", n.getOriginMac().c_str());
    TEST_ASSERT_EQUAL_STRING("Fall alert", n.getMessage().c_str());
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(n.getPendingPhones().size()));
    TEST_ASSERT_FALSE(n.isCompleted());
}

void test_mark_phone_as_sent_removes_only_that_phone() {
    QueuedNotification n = makeNotification();

    n.markPhoneAsSent("22222222222");

    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(n.getPendingPhones().size()));
    TEST_ASSERT_EQUAL_STRING("11111111111", n.getPendingPhones()[0].c_str());
    TEST_ASSERT_EQUAL_STRING("33333333333", n.getPendingPhones()[1].c_str());
}

void test_mark_phone_as_sent_ignores_unknown_phone() {
    QueuedNotification n = makeNotification();

    n.markPhoneAsSent("00000000000"); // was never pending

    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(n.getPendingPhones().size()));
}

void test_is_completed_becomes_true_once_every_phone_is_marked() {
    QueuedNotification n = makeNotification();

    n.markPhoneAsSent("11111111111");
    TEST_ASSERT_FALSE(n.isCompleted());

    n.markPhoneAsSent("22222222222");
    TEST_ASSERT_FALSE(n.isCompleted());

    n.markPhoneAsSent("33333333333");
    TEST_ASSERT_TRUE(n.isCompleted());
}

void test_default_constructed_notification_is_completed() {
    QueuedNotification n;
    TEST_ASSERT_TRUE(n.isCompleted());
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(n.getPendingPhones().size()));
}

void test_make_event_id_strips_colons_and_appends_timestamp() {
    String id = QueuedNotification::makeEventId("AA:BB:CC:DD:EE:FF", 1700000000);
    TEST_ASSERT_EQUAL_STRING("AABBCCDDEEFF_1700000000", id.c_str());
}

void test_make_event_id_differs_for_different_timestamps() {
    String idA = QueuedNotification::makeEventId("AA:BB:CC:DD:EE:FF", 1);
    String idB = QueuedNotification::makeEventId("AA:BB:CC:DD:EE:FF", 2);
    TEST_ASSERT_FALSE(idA.equals(idB));
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_constructor_stores_all_fields);
    RUN_TEST(test_mark_phone_as_sent_removes_only_that_phone);
    RUN_TEST(test_mark_phone_as_sent_ignores_unknown_phone);
    RUN_TEST(test_is_completed_becomes_true_once_every_phone_is_marked);
    RUN_TEST(test_default_constructed_notification_is_completed);
    RUN_TEST(test_make_event_id_strips_colons_and_appends_timestamp);
    RUN_TEST(test_make_event_id_differs_for_different_timestamps);
    UNITY_END();
}

void loop() {}
