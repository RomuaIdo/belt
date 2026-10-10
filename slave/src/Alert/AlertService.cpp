#include "Alert/AlertService.h"

AlertService::AlertService(uint32_t holdMs, NowMsFn nowMs)
    : holdMs(holdMs), nowMs(std::move(nowMs)) {}

void AlertService::onPress() {
    state = State::Holding;
    pressStartMs = nowMs();
}

bool AlertService::update(bool pressed) {
    if (state == State::Idle) return false;

    if (!pressed) {
        state = State::Idle;
        return false;
    }

    if (state == State::Holding && static_cast<uint32_t>(nowMs() - pressStartMs) >= holdMs) {
        state = State::Sending;
        return true;
    }
    return false;
}

void AlertService::abort() {
    state = State::Idle;
}
