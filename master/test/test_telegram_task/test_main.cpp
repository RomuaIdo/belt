// Executar: pio test -e esp32-s3-devkitc-1-test -f test_telegram_call
// Estes testes nao exigem Wi-Fi, credenciais ou envio de mensagens reais.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "Messaging/telegram_call.h"

void test_payload_uses_telegram_fields() {
    const String payload = createJsonPayload("123456789", "Alerta de queda");
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(doc.size()));
    TEST_ASSERT_TRUE(doc["chat_id"].is<String>());
    TEST_ASSERT_EQUAL_STRING("123456789", doc["chat_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Alerta de queda", doc["text"].as<const char*>());
}

void test_payload_preserves_negative_chat_id_and_escaped_unicode_text() {
    const String message = u8"Aten\u00e7\u00e3o: \"Maria\"\nC:\\alertas\t\U0001F6A8\r\n";
    const String payload = createJsonPayload("-1001234567890", message);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    TEST_ASSERT_EQUAL_STRING("-1001234567890", doc["chat_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(message.c_str(), doc["text"].as<const char*>());
}

void test_payload_rejects_empty_recipient_or_message() {
    TEST_ASSERT_TRUE(createJsonPayload("", "Alerta").isEmpty());
    TEST_ASSERT_TRUE(createJsonPayload("123456789", "").isEmpty());
    TEST_ASSERT_TRUE(createJsonPayload("", "").isEmpty());
}

void test_send_rejects_empty_inputs_before_network_access() {
    // O marcador nao e um certificado real: todos os casos devem sair antes do TLS.
    const char* unusedCertificate = "unused-certificate";
    const String payload = createJsonPayload("123456789", "Alerta");

    TEST_ASSERT_EQUAL_INT(-1, sendTelegramMessage("", payload, unusedCertificate));
    TEST_ASSERT_EQUAL_INT(-1, sendTelegramMessage("test-token", "", unusedCertificate));
    TEST_ASSERT_EQUAL_INT(-1, sendTelegramMessage("test-token", payload, nullptr));
    TEST_ASSERT_EQUAL_INT(-1, sendTelegramMessage("test-token", payload, ""));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_payload_uses_telegram_fields);
    RUN_TEST(test_payload_preserves_negative_chat_id_and_escaped_unicode_text);
    RUN_TEST(test_payload_rejects_empty_recipient_or_message);
    RUN_TEST(test_send_rejects_empty_inputs_before_network_access);
    UNITY_END();
}

void loop() {}
