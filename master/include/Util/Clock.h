#pragma once

#include <time.h>

// Has NTP synced yet? Any instant after 2023-11-14 indicates it has.
// TLS cert validation fails without a valid clock.
inline bool isClockValid() {
    return time(nullptr) > 1700000000;
}
