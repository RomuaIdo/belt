#pragma once

#include <cstdint>
#include <esp_wifi_types.h>

enum class WifiFailure : uint8_t {
    Other,             // Network gone, router busy, ...: not about the password
    HandshakeTimeout,  // What a wrong WPA2 password usually produces, but also a weak signal or a rebooting router
    AuthRejected,      // The router explicitly rejected the credentials
};

// Sorts a Wi-Fi disconnect reason into the categories above.
inline WifiFailure classifyWifiFailure(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_MIC_FAILURE:
            return WifiFailure::AuthRejected;
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return WifiFailure::HandshakeTimeout;
        default:
            return WifiFailure::Other;
    }
}

// Handshake timeouts, since the last connection or settings change, before the
// password is treated as wrong. A single one is not enough: it also happens with the
// right password when the signal is weak or the router is rebooting.
constexpr uint8_t WIFI_HANDSHAKE_TIMEOUT_LIMIT = 3;

// Decides, from the failed connection attempts so far, when the password is wrong.
class WifiFailureTracker {
public:
    // Records a failed attempt. Returns true when the password should be treated as wrong.
    bool recordFailure(uint8_t reason) {
        switch (classifyWifiFailure(reason)) {
            case WifiFailure::AuthRejected:
                return true;
            case WifiFailure::HandshakeTimeout:
                if (handshakeTimeouts < WIFI_HANDSHAKE_TIMEOUT_LIMIT) ++handshakeTimeouts;
                return handshakeTimeouts >= WIFI_HANDSHAKE_TIMEOUT_LIMIT;
            case WifiFailure::Other:
            default:
                return false;
        }
    }

    // Call after a successful connection or when the Wi-Fi settings change.
    void reset() { handshakeTimeouts = 0; }

    uint8_t handshakeTimeoutCount() const { return handshakeTimeouts; }

private:
    uint8_t handshakeTimeouts = 0;
};
