#include "Pairing/PairingService.h"

PairingService::PairingService(uint32_t timeoutMs, NowMsFn nowMs)
    : timeoutMs(timeoutMs), nowMs(std::move(nowMs)) {}

void PairingService::startSearching() {
    state = State::Searching;
    startMs = nowMs();
}

void PairingService::tick() {
    if (!isActive()) return;

    if (static_cast<uint32_t>(nowMs() - startMs) >= timeoutMs) {
        state = State::TimedOut;
    }
}

void PairingService::onPairWait() {
    if (isActive()) state = State::Waiting;
}

void PairingService::resumeSearching() {
    if (state == State::Waiting) state = State::Searching;
}

void PairingService::onPairAccepted() {
    if (isActive()) state = State::Paired;
}

void PairingService::reset() {
    if (state == State::Paired || state == State::TimedOut) {
        state = State::Idle;
    }
}

uint32_t PairingService::remainingMs() const {
    if (!isActive()) return 0;
    const uint32_t elapsed = static_cast<uint32_t>(nowMs() - startMs);
    return elapsed >= timeoutMs ? 0 : timeoutMs - elapsed;
}
