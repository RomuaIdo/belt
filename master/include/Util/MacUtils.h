#pragma once

#include <Arduino.h>

// Small, dependency-free helpers for parsing and formatting MAC addresses.

namespace MacUtils {

inline bool parse(const String& mac, uint8_t outBytes[6]) {
    if (mac.length() != 17) return false;
    unsigned int values[6];
    int matched = sscanf(mac.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x",
                          &values[0], &values[1], &values[2],
                          &values[3], &values[4], &values[5]);
    if (matched != 6) return false;
    for (int i = 0; i < 6; ++i) outBytes[i] = static_cast<uint8_t>(values[i]);
    return true;
}

inline String format(const uint8_t bytes[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
              bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return String(buf);
}

inline bool equal(const String& a, const String& b) {
    return a.equalsIgnoreCase(b);
}

} // namespace MacUtils
