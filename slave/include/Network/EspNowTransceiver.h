#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <functional>

// Thin, native wrapper around the ESP-IDF ESP-NOW radio API. Mirrors
// master/'s EspNowTransceiver byte-for-byte, minus sendAck (no inbound
// traffic to ack here). A static instance pointer routes the C-style RX
// callback back into this object.
class EspNowTransceiver {
public:
    using ReceiveCallback = std::function<void(const String& senderMac, const uint8_t* data, int len)>;

    bool init();
    bool registerPeer(const String& macAddress);
    bool send(const String& targetMac, const uint8_t* data, size_t len);

    void setOnMessageReceived(ReceiveCallback callback) { onMessageReceived = callback; }

private:
    ReceiveCallback onMessageReceived;

    void handleRxInterrupt(const uint8_t mac[6], const uint8_t* data, int len);

    // Matches esp_now_recv_cb_t as defined by the arduino-esp32 core in use
    // (pre-IDF5 signature: raw sender MAC, no esp_now_recv_info_t wrapper).
    static void onDataRecvTrampoline(const uint8_t* mac, const uint8_t* data, int len);
    static EspNowTransceiver* instance;
};
