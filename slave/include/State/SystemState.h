#pragma once

#include <Arduino.h>

#include "Alert/AlertService.h"
#include "Pairing/PairingService.h"

// What the belt is doing right now. Only ACTIVE states exist: Idle means nothing
// is happening and every output is off (the belt can sleep).
enum class SystemState : uint8_t {
    Idle,
    PairingSearching,  // scanning channels for the master
    PairingWaiting,    // master answered, waiting for the caregiver to press Save
    AlertHolding,      // alert button pressed, hold time not reached yet
    AlertSending,      // alert sent, button still held
};

// A transition: short feedback that plays once and ends by itself.
enum class SystemEvent : uint8_t {
    PairingSucceeded,
    PairingTimedOut,
    AlertSent,
    AlertRejected,  // no master to send to
    BootFault,      // radio or IMU failed to start
};

// What the subsystems report; the StateHandler derives the SystemState from it.
struct SystemSnapshot {
    PairingService::State pairing = PairingService::State::Idle;
    AlertService::State alert = AlertService::State::Idle;
};

// Priority: alert > pairing > idle.
inline SystemState resolveState(const SystemSnapshot& snapshot) {
    switch (snapshot.alert) {
        case AlertService::State::Holding: return SystemState::AlertHolding;
        case AlertService::State::Sending: return SystemState::AlertSending;
        case AlertService::State::Idle: break;
    }
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
        case SystemState::AlertHolding: return "AlertHolding";
        case SystemState::AlertSending: return "AlertSending";
    }
    return "?";
}

inline const char* toString(SystemEvent event) {
    switch (event) {
        case SystemEvent::PairingSucceeded: return "PairingSucceeded";
        case SystemEvent::PairingTimedOut: return "PairingTimedOut";
        case SystemEvent::AlertSent: return "AlertSent";
        case SystemEvent::AlertRejected: return "AlertRejected";
        case SystemEvent::BootFault: return "BootFault";
    }
    return "?";
}
