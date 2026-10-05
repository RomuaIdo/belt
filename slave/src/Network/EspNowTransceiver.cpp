#include "Network/EspNowTransceiver.h"
#include "Util/MacUtils.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <cstring>

EspNowTransceiver* EspNowTransceiver::instance = nullptr;

bool EspNowTransceiver::init() {
    instance = this;

    if (esp_now_init() != ESP_OK) {
        Serial.println("EspNowTransceiver: esp_now_init failed");
        return false;
    }

    esp_now_register_recv_cb(onDataRecvTrampoline);
    return true;
}

bool EspNowTransceiver::registerPeer(const String& macAddress) {
    uint8_t mac[6];
    if (!MacUtils::parse(macAddress, mac)) return false;

    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0; // Current interface channel
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;

    return esp_now_add_peer(&peerInfo) == ESP_OK;
}

bool EspNowTransceiver::send(const String& targetMac, const uint8_t* data, size_t len) {
    if (!registerPeer(targetMac)) return false;

    uint8_t mac[6];
    if (!MacUtils::parse(targetMac, mac)) return false;

    return esp_now_send(mac, data, len) == ESP_OK;
}

bool EspNowTransceiver::setChannel(uint8_t channel) {
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

void EspNowTransceiver::handleRxInterrupt(const uint8_t mac[6], const uint8_t* data, int len) {
    if (!onMessageReceived) return;
    onMessageReceived(MacUtils::format(mac), data, len);
}

void EspNowTransceiver::onDataRecvTrampoline(const uint8_t* mac, const uint8_t* data, int len) {
    if (instance == nullptr || mac == nullptr) return;
    instance->handleRxInterrupt(mac, data, len);
}
