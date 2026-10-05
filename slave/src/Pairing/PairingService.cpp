#include "Pairing/PairingService.h"

PairingService::PairingService(uint32_t listenWindowMs, NowMsFn nowMs)
    : listenWindowMs(listenWindowMs), nowMs(std::move(nowMs)) {}

void PairingService::startListening() {
    state = State::Listening;
    windowStartMs = nowMs();
}

void PairingService::tick() {
    if (state != State::Listening) return;

    if (static_cast<uint32_t>(nowMs() - windowStartMs) >= listenWindowMs) {
        state = State::TimedOut;
    }
}

void PairingService::onPairRequestReceived() {
    if (state != State::Listening) return; // stray/duplicate request outside a listening window: ignore
    state = State::Paired;
}

void PairingService::reset() {
    if (state == State::Paired || state == State::TimedOut) {
        state = State::Idle;
    }
}

uint32_t PairingService::remainingMs() const {
    if (state != State::Listening) return 0;
    uint32_t elapsed = static_cast<uint32_t>(nowMs() - windowStartMs);
    return elapsed >= listenWindowMs ? 0 : listenWindowMs - elapsed;
}
