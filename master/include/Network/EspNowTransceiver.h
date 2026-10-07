#pragma once

#include <Arduino.h>
#include <array>
#include <vector>

#include "Protocol/Message.h"

// Frame received over ESP-NOW, copied by the radio callback into the RX queue.
struct RadioFrame {
    uint8_t mac[6];   // sender, reported by the radio (never read from the payload)
    uint16_t len;     // real length; `data` keeps only the first bytes
    uint8_t data[Protocol::kMaxFrameSize];
};

// Wrapper around the ESP-IDF ESP-NOW API.
//
// The receive callback runs on the Wi-Fi task, not in a hardware interrupt, so it
// must not block or touch application state. It only copies the frame into a
// FreeRTOS queue; the loop() task drains it with receive().
class EspNowTransceiver {
public:
    // Call after WiFi.mode(). Returns false if ESP-NOW fails to start.
    bool init();

    // Next queued frame, waiting up to `timeoutMs`. False if the queue is empty.
    bool receive(RadioFrame& out, uint32_t timeoutMs = 0);

    // Unicast through the STA interface on the radio's current channel. The peer is
    // registered on demand. No delivery guarantee: callers wait for a reply.
    bool send(const String& targetMac, const uint8_t* data, size_t len);

    // Wi-Fi channel the master is on (the router's, or its own network's).
    static uint8_t channel();

private:
    // ESP-NOW holds few peers (20): when full, the oldest is dropped and registered
    // again the next time it is needed.
    static constexpr size_t kMaxPeers = 10;

    bool ensurePeer(const uint8_t mac[6]);

    std::vector<std::array<uint8_t, 6>> peers;
};
