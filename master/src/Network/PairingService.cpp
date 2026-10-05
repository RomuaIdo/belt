#include "Network/PairingService.h"
#include "Util/MacUtils.h"

PairingService::PairingService(uint32_t confirmTimeoutMs, NowMsFn nowMs)
    : confirmTimeoutMs(confirmTimeoutMs), nowMs(std::move(nowMs)) {}

void PairingService::start(const String& mac) {
    active = true;
    targetMac = mac;
    startMs = nowMs();
}

void PairingService::stop() {
    active = false;
}

void PairingService::confirm() {
    active = false;
}

void PairingService::tick() {
    if (!active) return;
    if (static_cast<uint32_t>(nowMs() - startMs) >= confirmTimeoutMs) {
        active = false;
    }
}

bool PairingService::isPendingFor(const String& mac) const {
    return active && MacUtils::equal(targetMac, mac);
}

uint32_t PairingService::remainingMs() const {
    if (!active) return 0;
    uint32_t elapsed = static_cast<uint32_t>(nowMs() - startMs);
    return elapsed >= confirmTimeoutMs ? 0 : confirmTimeoutMs - elapsed;
}
