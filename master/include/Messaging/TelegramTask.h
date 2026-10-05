#pragma once

#include <Arduino.h>
#include <atomic>

// Background Telegram send task (lifecycle: free -> sending -> done -> free).
class TelegramTask {
public:
    // Builds sendMessage JSON body for the given chat ID.
    static String createJsonPayload(const String& chatId, const String& message);

    // Spawns background task to send message; requires Wi-Fi and synced clock.
    bool startTask(const String& token, const String& event, const String& chat,
               const String& message);

    // Synchronous request to Telegram Bot API; returns HTTP code or negative on error.
    static int request(const String& token, const char* method, const String& jsonBody,
                       String* response = nullptr);

    bool isFree() const { return !busy; }
    bool isDone() const { return busy && httpStatus != 0; }
    void release() { busy = false; }

    // HTTP status code (or negative error) once isDone().
    int getHttpStatus() const { return httpStatus; }
    const String& getEventId() const { return eventId; }
    const String& getChatId() const { return chatId; }

private:
    static void run(void* self);  // FreeRTOS task entry

    bool busy = false;
    String botToken;
    String eventId;
    String chatId;
    String payload;
    std::atomic<int> httpStatus{0};  // 0 while running
};
