#include "Fall/FallService.h"

FallService::FallService(uint32_t cancelWindowMs, NowMsFn nowMs)
    : cancelWindowMs(cancelWindowMs), nowMs(std::move(nowMs)) {}

void FallService::onFallDetected() {
    if (state == State::Countdown) return;
    state = State::Countdown;
    startMs = nowMs();
}

bool FallService::cancel() {
    if (state != State::Countdown) return false;
    state = State::Idle;
    return true;
}

bool FallService::update() {
    if (state != State::Countdown) return false;
    if (static_cast<uint32_t>(nowMs() - startMs) < cancelWindowMs) return false;
    state = State::Idle;
    return true;
}
