#pragma once

#include <Arduino.h>

#include "Fall/FallService.h"
#include "Pairing/PairingService.h"

// What the belt is doing right now. Only ACTIVE states exist: Idle means nothing
// is happening and every output is off (the belt can sleep).
enum class SystemState : uint8_t {
    Idle,
    PairingSearching,  // scanning channels for the master
    PairingWaiting,    // master answered, waiting for the caregiver to press Save
    FallCountdown,     // fall confirmed: the alert goes out unless the wearer cancels
};

// A transition: short feedback that plays once and ends by itself.
enum class SystemEvent : uint8_t {
    PairingSucceeded,
    PairingTimedOut,
    AlertSent,
    AlertRejected,  // no master to send to
    FallCancelled,  // wearer cancelled the fall countdown
    BootFault,      // radio or IMU failed to start
};

// What the subsystems report; the StateHandler derives the SystemState from it.
struct SystemSnapshot {
    PairingService::State pairing = PairingService::State::Idle;
    FallService::State fall = FallService::State::Idle;
};

// Priority: fall > pairing > idle.
inline SystemState resolveState(const SystemSnapshot& snapshot) {
    if (snapshot.fall == FallService::State::Countdown) return SystemState::FallCountdown;
    switch (snapshot.pairing) {
        case PairingService::State::Searching: return SystemState::PairingSearching;
        case PairingService::State::Waiting: return SystemState::PairingWaiting;
        default: break;  // Idle, Paired, TimedOut: nothing to show (events cover the latter two)
    }
    return SystemState::Idle;
}

inline const char* toString(SystemState state) {
    switch (state) {
        case SystemState::Idle: return "Idle";
        case SystemState::PairingSearching: return "PairingSearching";
        case SystemState::PairingWaiting: return "PairingWaiting";
        case SystemState::FallCountdown: return "FallCountdown";
    }
    return "?";
}

inline const char* toString(SystemEvent event) {
    switch (event) {
        case SystemEvent::PairingSucceeded: return "PairingSucceeded";
        case SystemEvent::PairingTimedOut: return "PairingTimedOut";
        case SystemEvent::AlertSent: return "AlertSent";
        case SystemEvent::AlertRejected: return "AlertRejected";
        case SystemEvent::FallCancelled: return "FallCancelled";
        case SystemEvent::BootFault: return "BootFault";
    }
    return "?";
}
