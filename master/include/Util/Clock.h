#pragma once

#include <time.h>

// Checks if NTP time is synced (valid clock required for TLS).
inline bool isClockValid() {
    return time(nullptr) > 1700000000;
}
