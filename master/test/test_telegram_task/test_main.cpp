// Run: pio test -e esp32-s3-devkitc-1-test -f test_telegram_task

#include <Arduino.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "Messaging/TelegramTask.h"

void test_payload_uses_telegram_fields() {
    const String payload = TelegramTask::createJsonPayload("123456789", "Alerta de queda");
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(doc.size()));
    TEST_ASSERT_TRUE(doc["chat_id"].is<String>());
    TEST_ASSERT_EQUAL_STRING("123456789", doc["chat_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Alerta de queda", doc["text"].as<const char*>());
}

void test_payload_preserves_negative_chat_id_and_escaped_unicode_text() {
    const String message = u8"Atenção: \"Maria\"\nC:\\alertas\t\U0001F6A8\r\n";
    const String payload = TelegramTask::createJsonPayload("-1001234567890", message);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    TEST_ASSERT_EQUAL_STRING("-1001234567890", doc["chat_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(message.c_str(), doc["text"].as<const char*>());
}

void test_payload_rejects_empty_recipient_or_message() {
    TEST_ASSERT_TRUE(TelegramTask::createJsonPayload("", "Alerta").isEmpty());
    TEST_ASSERT_TRUE(TelegramTask::createJsonPayload("123456789", "").isEmpty());
    TEST_ASSERT_TRUE(TelegramTask::createJsonPayload("", "").isEmpty());
}

void test_start_rejects_empty_inputs_before_creating_task() {
    TelegramTask task;

    TEST_ASSERT_FALSE(task.startTask("", "evt1", "123456789", "Alerta"));
    TEST_ASSERT_FALSE(task.startTask("test-token", "", "123456789", "Alerta"));
    TEST_ASSERT_FALSE(task.startTask("test-token", "evt1", "", "Alerta"));
    TEST_ASSERT_FALSE(task.startTask("test-token", "evt1", "123456789", ""));

    // Object remains free.
    TEST_ASSERT_TRUE(task.isFree());
    TEST_ASSERT_FALSE(task.isDone());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_payload_uses_telegram_fields);
    RUN_TEST(test_payload_preserves_negative_chat_id_and_escaped_unicode_text);
    RUN_TEST(test_payload_rejects_empty_recipient_or_message);
    RUN_TEST(test_start_rejects_empty_inputs_before_creating_task);
    UNITY_END();
}

void loop() {}
