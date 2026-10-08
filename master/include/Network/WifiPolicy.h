#pragma once

#include <cstdint>
#include <esp_wifi_types.h>

// Returns true if disconnect reason indicates authentication or handshake failure.
inline bool isWifiAuthFailure(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
            return true;
        default:
            return false;
    }
}
