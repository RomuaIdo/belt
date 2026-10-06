// Unit tests for Notification state object.
// Run: pio test -e esp32-s3-devkitc-1-test -f test_notification

#include <Arduino.h>
#include <unity.h>
#include <vector>

#include "Domain/Notification.h"

namespace {

Notification makeNotification() {
    std::vector<String> chatIds = {"123456789", "-1001234567890", "5000000000"};
    return Notification("evt123", 1700000000, "AA:BB:CC:DD:EE:FF", "Fall alert", chatIds);
}

} // namespace

void test_constructor_stores_all_fields() {
    Notification n = makeNotification();

    TEST_ASSERT_EQUAL_STRING("evt123", n.getEventId().c_str());
    TEST_ASSERT_EQUAL_UINT32(1700000000, n.getTimestamp());
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", n.getOriginMac().c_str());
    TEST_ASSERT_EQUAL_STRING("Fall alert", n.getMessage().c_str());
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(n.getPendingChatIds().size()));
    TEST_ASSERT_EQUAL_STRING("123456789", n.getPendingChatIds()[0].c_str());
    TEST_ASSERT_EQUAL_STRING("-1001234567890", n.getPendingChatIds()[1].c_str());
    TEST_ASSERT_EQUAL_STRING("5000000000", n.getPendingChatIds()[2].c_str());
    TEST_ASSERT_FALSE(n.isCompleted());
}

void test_mark_chat_as_sent_removes_only_that_chat() {
    Notification n = makeNotification();

    n.markChatAsSent("-1001234567890");

    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(n.getPendingChatIds().size()));
    TEST_ASSERT_EQUAL_STRING("123456789", n.getPendingChatIds()[0].c_str());
    TEST_ASSERT_EQUAL_STRING("5000000000", n.getPendingChatIds()[1].c_str());
}

void test_mark_chat_as_sent_ignores_unknown_chat() {
    Notification n = makeNotification();

    n.markChatAsSent("987654321"); // Not pending

    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(n.getPendingChatIds().size()));
}

void test_is_completed_becomes_true_once_every_chat_is_marked() {
    Notification n = makeNotification();

    n.markChatAsSent("123456789");
    TEST_ASSERT_FALSE(n.isCompleted());

    n.markChatAsSent("-1001234567890");
    TEST_ASSERT_FALSE(n.isCompleted());

    n.markChatAsSent("5000000000");
    TEST_ASSERT_TRUE(n.isCompleted());
}

void test_default_constructed_notification_is_completed() {
    Notification n;
    TEST_ASSERT_TRUE(n.isCompleted());
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(n.getPendingChatIds().size()));
}

void test_make_event_id_strips_colons_and_appends_timestamp() {
    String id = Notification::makeEventId("AA:BB:CC:DD:EE:FF", 1700000000);
    TEST_ASSERT_EQUAL_STRING("AABBCCDDEEFF_1700000000", id.c_str());
}

void test_make_event_id_differs_for_different_timestamps() {
    String idA = Notification::makeEventId("AA:BB:CC:DD:EE:FF", 1);
    String idB = Notification::makeEventId("AA:BB:CC:DD:EE:FF", 2);
    TEST_ASSERT_FALSE(idA.equals(idB));
}

void setup() {
    delay(2000); // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_constructor_stores_all_fields);
    RUN_TEST(test_mark_chat_as_sent_removes_only_that_chat);
    RUN_TEST(test_mark_chat_as_sent_ignores_unknown_chat);
    RUN_TEST(test_is_completed_becomes_true_once_every_chat_is_marked);
    RUN_TEST(test_default_constructed_notification_is_completed);
    RUN_TEST(test_make_event_id_strips_colons_and_appends_timestamp);
    RUN_TEST(test_make_event_id_differs_for_different_timestamps);
    UNITY_END();
}

void loop() {}
