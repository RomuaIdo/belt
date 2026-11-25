// Unit tests for AlertLog (the alerts listed on the config page).
// Run: pio test -e esp32-s3-devkitc-1-test -f test_alert_log

#include <Arduino.h>
#include <unity.h>

#include "Domain/AlertLog.h"

namespace {

AlertLog::Entry makeEntry(const String& id, std::initializer_list<const char*> chats = {"111", "222"}) {
    AlertLog::Entry entry;
    entry.id = id;
    entry.mac = "AA:BB:CC:DD:EE:FF";
    entry.name = "Maria";
    for (const char* chatId : chats) {
        AlertLog::Chat chat;
        chat.chatId = chatId;
        entry.chats.push_back(chat);
    }
    return entry;
}

}  // namespace

void test_every_alert_is_kept_separately() {
    // Repeated falls of the same belt are separate entries, oldest first.
    AlertLog log;
    log.add(makeEntry("a"));
    log.add(makeEntry("b"));
    log.add(makeEntry("c"));
    TEST_ASSERT_EQUAL_UINT32(3, log.entries().size());
    TEST_ASSERT_EQUAL_STRING("a", log.entries().front().id.c_str());
    TEST_ASSERT_EQUAL_STRING("c", log.entries().back().id.c_str());
}

void test_new_chats_start_as_sending() {
    AlertLog log;
    log.add(makeEntry("a"));
    for (const auto& chat : log.entries()[0].chats) {
        TEST_ASSERT_TRUE(chat.status == AlertLog::ChatStatus::Sending);
        TEST_ASSERT_EQUAL_INT(0, chat.httpStatus);
    }
}

void test_oldest_dropped_at_capacity() {
    AlertLog log;
    for (size_t i = 0; i < AlertLog::CAPACITY + 2; ++i) log.add(makeEntry(String(i)));
    TEST_ASSERT_EQUAL_UINT32(AlertLog::CAPACITY, log.entries().size());
    TEST_ASSERT_EQUAL_STRING("2", log.entries().front().id.c_str());
    TEST_ASSERT_FALSE(log.contains("0"));
}

void test_set_chat_status_updates_only_that_chat() {
    AlertLog log;
    log.add(makeEntry("a"));
    log.setChatStatus("a", "222", AlertLog::ChatStatus::Sent, 200);
    const auto& chats = log.entries()[0].chats;
    TEST_ASSERT_TRUE(chats[0].status == AlertLog::ChatStatus::Sending);
    TEST_ASSERT_TRUE(chats[1].status == AlertLog::ChatStatus::Sent);
    TEST_ASSERT_EQUAL_INT(200, chats[1].httpStatus);
}

void test_delivery_time_kept_only_when_sent() {
    AlertLog log;
    log.add(makeEntry("a"));
    log.setChatStatus("a", "111", AlertLog::ChatStatus::Sent, 200, 1800000000);
    log.setChatStatus("a", "222", AlertLog::ChatStatus::Retrying, -1, 1800000000);
    const auto& chats = log.entries()[0].chats;
    TEST_ASSERT_EQUAL_UINT32(1800000000, chats[0].deliveredAtEpoch);
    TEST_ASSERT_EQUAL_UINT32(0, chats[1].deliveredAtEpoch);
}

void test_set_chat_status_ignores_unknown_ids() {
    AlertLog log;
    log.add(makeEntry("a"));
    log.setChatStatus("zzz", "111", AlertLog::ChatStatus::Sent, 200);  // dismissed alert
    log.setChatStatus("a", "999", AlertLog::ChatStatus::Sent, 200);    // unknown chat
    for (const auto& chat : log.entries()[0].chats) {
        TEST_ASSERT_TRUE(chat.status == AlertLog::ChatStatus::Sending);
    }
}

void test_remove_and_clear() {
    AlertLog log;
    log.add(makeEntry("a"));
    log.add(makeEntry("b"));
    TEST_ASSERT_TRUE(log.remove("a"));
    TEST_ASSERT_FALSE(log.remove("a"));
    TEST_ASSERT_EQUAL_UINT32(1, log.entries().size());
    log.clear();
    TEST_ASSERT_TRUE(log.entries().empty());
}

void test_same_id_replaces_entry() {
    AlertLog log;
    log.add(makeEntry("a", {"111"}));
    log.add(makeEntry("a", {"111", "222"}));
    TEST_ASSERT_EQUAL_UINT32(1, log.entries().size());
    TEST_ASSERT_EQUAL_UINT32(2, log.entries()[0].chats.size());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_every_alert_is_kept_separately);
    RUN_TEST(test_new_chats_start_as_sending);
    RUN_TEST(test_oldest_dropped_at_capacity);
    RUN_TEST(test_set_chat_status_updates_only_that_chat);
    RUN_TEST(test_delivery_time_kept_only_when_sent);
    RUN_TEST(test_set_chat_status_ignores_unknown_ids);
    RUN_TEST(test_remove_and_clear);
    RUN_TEST(test_same_id_replaces_entry);
    UNITY_END();
}

void loop() {}
