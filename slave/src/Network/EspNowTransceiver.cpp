#include "Network/EspNowTransceiver.h"

#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Util/MacUtils.h"

namespace {
constexpr UBaseType_t kRxQueueLength = 16;

QueueHandle_t rxQueue = nullptr;

// Runs on the Wi-Fi task: no logging, allocation or waiting. A full queue drops the frame.
void enqueueFrame(const uint8_t* mac, const uint8_t* data, int len) {
    if (rxQueue == nullptr || mac == nullptr || data == nullptr || len <= 0) return;

    RadioFrame frame;
    memcpy(frame.mac, mac, sizeof(frame.mac));
    frame.len = static_cast<uint16_t>(len);
    // A frame longer than the largest defined one is rejected by parseFrame() on
    // length alone, so keeping its beginning is enough.
    memcpy(frame.data, data, min(static_cast<size_t>(len), sizeof(frame.data)));
    xQueueSend(rxQueue, &frame, 0);
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    enqueueFrame(info->src_addr, data, len);
}
#else
void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    enqueueFrame(mac, data, len);
}
#endif
}  // namespace

bool EspNowTransceiver::init() {
    if (rxQueue == nullptr) {
        rxQueue = xQueueCreate(kRxQueueLength, sizeof(RadioFrame));
        if (rxQueue == nullptr) return false;
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("EspNowTransceiver: esp_now_init failed");
        return false;
    }
    return esp_now_register_recv_cb(onReceive) == ESP_OK;
}

bool EspNowTransceiver::receive(RadioFrame& out, uint32_t timeoutMs) {
    return rxQueue != nullptr && xQueueReceive(rxQueue, &out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool EspNowTransceiver::send(const String& targetMac, const uint8_t* data, size_t len) {
    uint8_t mac[6];
    if (!MacUtils::parse(targetMac, mac)) return false;
    return ensurePeer(mac) && esp_now_send(mac, data, len) == ESP_OK;
}

bool EspNowTransceiver::setChannel(uint8_t channel) {
    if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;

    uint8_t primary = 0;
    wifi_second_chan_t second;
    return esp_wifi_get_channel(&primary, &second) == ESP_OK && primary == channel;
}

bool EspNowTransceiver::ensurePeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) return true;

    if (peers.size() >= kMaxPeers) {
        esp_now_del_peer(peers.front().data());
        peers.erase(peers.begin());
    }

    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, mac, sizeof(info.peer_addr));
    info.channel = 0;  // 0 = the radio's current channel
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;
    if (esp_now_add_peer(&info) != ESP_OK) return false;

    std::array<uint8_t, 6> entry;
    memcpy(entry.data(), mac, entry.size());
    peers.push_back(entry);
    return true;
}
