#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <functional>

// Native wrapper around ESP-IDF ESP-NOW radio API.
class EspNowTransceiver {
public:
    using ReceiveCallback = std::function<void(const String& senderMac, const uint8_t* data, int len)>;

    bool init();
    bool registerPeer(const String& macAddress);
    bool send(const String& targetMac, const uint8_t* data, size_t len);
    bool sendAck(const String& targetMac, uint8_t status);

    void setOnMessageReceived(ReceiveCallback callback) { onMessageReceived = callback; }

private:
    ReceiveCallback onMessageReceived;

    void handleRxInterrupt(const uint8_t mac[6], const uint8_t* data, int len);

    // Pre-IDF5 esp_now_recv_cb_t callback trampoline.
    static void onDataRecvTrampoline(const uint8_t* mac, const uint8_t* data, int len);
    static EspNowTransceiver* instance;
};
