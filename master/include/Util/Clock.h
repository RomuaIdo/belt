#pragma once

#include <time.h>

// O NTP ja sincronizou? Qualquer instante apos 2023-11-14 indica que sim.
// Sem relogio valido a verificacao do certificado TLS falha.
inline bool isClockValid() {
    return time(nullptr) > 1700000000;
}
