#include "Messaging/TelegramTask.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Messaging/telegram_certificate.h"

namespace {
// No ESP-IDF, o tamanho da pilha e informado em bytes. Validar a folga na placa.
constexpr uint32_t TASK_STACK_BYTES = 8192;
constexpr UBaseType_t TASK_PRIORITY = 1;

constexpr uint16_t HTTP_TIMEOUT_MS = 10000;
constexpr unsigned long TLS_HANDSHAKE_TIMEOUT_SECONDS = 10;
}

String TelegramTask::createJsonPayload(const String& chatId, const String& message) {
    if (chatId.isEmpty() || message.isEmpty()) {
        return String();
    }

    JsonDocument doc;
    doc["chat_id"] = chatId;
    doc["text"] = message;
    if (doc.overflowed()) {
        return String();
    }

    String payload;
    const size_t expectedSize = measureJson(doc);
    if (!payload.reserve(expectedSize) || serializeJson(doc, payload) != expectedSize) {
        return String();
    }
    return payload;
}

bool TelegramTask::startTask(const String& token, const String& event, const String& chat,
                         const String& message) {
    if (busy || token.isEmpty() || event.isEmpty()) {
        return false;
    }
    const String newPayload = createJsonPayload(chat, message);
    if (newPayload.isEmpty()) {
        return false;
    }

    // Preencher tudo antes do xTaskCreate: a task pode rodar antes de ele retornar.
    botToken = token;
    eventId = event;
    chatId = chat;
    payload = newPayload;
    httpStatus = 0;
    busy = true;

    if (xTaskCreate(run, "telegramSend", TASK_STACK_BYTES, this, TASK_PRIORITY, nullptr) != pdPASS) {
        busy = false;
        return false;
    }
    return true;
}

void TelegramTask::run(void* self) {
    auto* task = static_cast<TelegramTask*>(self);
    task->httpStatus = request(task->botToken, "sendMessage", task->payload);
    vTaskDelete(nullptr);  // uma task nunca pode dar return
}

int TelegramTask::request(const String& token, const char* method, const String& jsonBody,
                          String* response) {
    if (token.isEmpty()) {
        return -1;
    }

    WiFiClientSecure client;
    client.setCACert(TELEGRAM_ROOT_CA);
    client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_SECONDS);

    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    const String url = String("https://api.telegram.org/bot") + token + "/" + method;

    if (!http.begin(client, url)) {
        http.end();
        return -1;
    }

    http.addHeader("Content-Type", "application/json");
    const int statusCode = http.POST(jsonBody);
    if (response != nullptr && statusCode > 0) {
        *response = http.getString();
    }
    http.end();
    return statusCode;
}
