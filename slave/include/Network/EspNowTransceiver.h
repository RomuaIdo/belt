#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <functional>

// Wrapper around ESP-IDF ESP-NOW API.
class EspNowTransceiver {
public:
    using ReceiveCallback = std::function<void(const String& senderMac, const uint8_t* data, int len)>;

    bool init();
    bool registerPeer(const String& macAddress);
    bool send(const String& targetMac, const uint8_t* data, size_t len);
    // Tunes Wi-Fi channel used by ESP-NOW.
    bool setChannel(uint8_t channel);

    void setOnMessageReceived(ReceiveCallback callback) { onMessageReceived = callback; }

private:
    ReceiveCallback onMessageReceived;

    void handleRxInterrupt(const uint8_t mac[6], const uint8_t* data, int len);

    // C-callback trampoline for ESP-NOW RX.
    static void onDataRecvTrampoline(const uint8_t* mac, const uint8_t* data, int len);
    static EspNowTransceiver* instance;
};
