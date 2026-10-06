#include "Pairing/PairingService.h"

PairingService::PairingService(uint32_t searchTimeoutMs, NowMsFn nowMs)
    : searchTimeoutMs(searchTimeoutMs), nowMs(std::move(nowMs)) {}

void PairingService::startSearching() {
    state = State::Searching;
    searchStartMs = nowMs();
}

void PairingService::tick() {
    if (state != State::Searching) return;

    if (static_cast<uint32_t>(nowMs() - searchStartMs) >= searchTimeoutMs) {
        state = State::TimedOut;
    }
}

void PairingService::onPairResponseReceived() {
    if (state != State::Searching) return; // Ignore stray response
    state = State::Paired;
}

void PairingService::reset() {
    if (state == State::Paired || state == State::TimedOut) {
        state = State::Idle;
    }
}

uint32_t PairingService::remainingMs() const {
    if (state != State::Searching) return 0;
    uint32_t elapsed = static_cast<uint32_t>(nowMs() - searchStartMs);
    return elapsed >= searchTimeoutMs ? 0 : searchTimeoutMs - elapsed;
}
