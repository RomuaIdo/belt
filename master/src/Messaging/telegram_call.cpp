#include "Messaging/telegram_call.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {
constexpr uint16_t HTTP_TIMEOUT_MS = 10000;
constexpr unsigned long TLS_HANDSHAKE_TIMEOUT_SECONDS = 10;
}

int sendTelegramMessage(const String& botToken, const String& jsonPayload,
                        const char* caCertificate) {
    if (botToken.isEmpty() || jsonPayload.isEmpty() ||
        caCertificate == nullptr || caCertificate[0] == '\0') {
        return -1;
    }

    WiFiClientSecure client;
    client.setCACert(caCertificate);
    client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_SECONDS);

    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    const String url = String("https://api.telegram.org/bot") + botToken + "/sendMessage";

    if (!http.begin(client, url)) {
        http.end();
        return -1;
    }

    http.addHeader("Content-Type", "application/json");
    const int statusCode = http.POST(jsonPayload);
    http.end();
    return statusCode;
}

String createJsonPayload(const String& chatId, const String& message) {
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
