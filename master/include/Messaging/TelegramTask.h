#pragma once

#include <Arduino.h>
#include <atomic>

// A single Telegram send running on its own FreeRTOS task.
// Lifecycle: free -> sending (start) -> done (isDone) -> free (release).
// Not copyable/movable (the task holds its address), so it must live in a
// fixed-size array.
class TelegramTask {
public:
    // Builds the sendMessage JSON body; empty if chatId or message is empty.
    // chatId is a Telegram chat identifier, not a phone number.
    static String createJsonPayload(const String& chatId, const String& message);

    // Starts the send in the background, without waiting for the response.
    // Returns false if already busy, if any input is empty, or if the task
    // couldn't be created. Requires Wi-Fi and a valid clock (TLS cert check).
    bool startTask(const String& token, const String& event, const String& chat,
               const String& message);

    // Blocking call (up to ~30s on network failure) to any Telegram Bot API
    // method, e.g. "getMe" or "getUpdates". Returns the HTTP status, or a
    // negative value on a local/network failure. `response` gets the body if not null.
    static int request(const String& token, const char* method, const String& jsonBody,
                       String* response = nullptr);

    bool isFree() const { return !busy; }
    bool isDone() const { return busy && httpStatus != 0; }
    void release() { busy = false; }

    // Valid once isDone(): HTTP status, or negative on network failure.
    int getHttpStatus() const { return httpStatus; }
    const String& getEventId() const { return eventId; }
    const String& getChatId() const { return chatId; }

private:
    static void run(void* self);  // xTaskCreate bridge

    bool busy = false;
    String botToken;
    String eventId;
    String chatId;
    String payload;
    std::atomic<int> httpStatus{0};  // 0 while the task is running
};
