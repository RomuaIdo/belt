#pragma once

#include <Arduino.h>

#include "State/Feedback.h"
#include "State/PatternPlayer.h"
#include "State/SystemState.h"

// Knows the belt's state and drives the outputs (LED, buzzer, vibration motor).
//
// It only acts while a state is active or an event is playing: in Idle, with no
// event playing, update() does nothing (the belt can sleep). What each output does
// is decided by the tables in State/Feedback.h.
//
//   state: update() derives it from the subsystems' snapshot; its feedback plays
//          for as long as the state lasts (pairing -> LED blinks).
//   event: notify() plays a short feedback over the state's, only on the outputs
//          it uses, then those outputs go back to the state's (paired -> quick beeps).
//
// An output whose pin is AppConfig::kNoPin is skipped.
class StateHandler {
public:
    StateHandler(uint8_t ledPin, uint8_t buzzerPin, uint8_t vibrationPin);

    void begin();  // configures the pins and turns every output off

    void update(const SystemSnapshot& snapshot, uint32_t nowMs);
    void notify(SystemEvent event, uint32_t nowMs);

    SystemState state() const { return current; }

    // Idle and no event playing: nothing is happening.
    bool isIdle(uint32_t nowMs) const;

    // What the outputs were last told (also kept for pins that are kNoPin).
    bool ledIsOn() const { return led.on; }
    bool buzzerIsOn() const { return buzzer.on; }
    bool vibrationIsOn() const { return vibration.on; }

private:
    struct Channel {
        PatternPlayer statePlayer;
        PatternPlayer eventPlayer;
        bool eventActive = false;
        bool on = false;  // last level written

        void startEvent(const Pattern& pattern, uint32_t nowMs);
        bool eventPlaying(uint32_t nowMs) const;
        bool wantsOn(uint32_t nowMs);  // retires a finished event, then asks the active player
    };

    void applyState(uint32_t nowMs);
    void render(uint32_t nowMs);
    void writeLed(bool on, const Rgb& color);
    void writeBuzzer(bool on);
    void writeVibration(bool on);

    uint8_t ledPin;
    uint8_t buzzerPin;
    uint8_t vibrationPin;

    SystemState current = SystemState::Idle;
    Channel led;
    Channel buzzer;
    Channel vibration;
    Rgb stateColor;
    Rgb eventColor;
    Rgb lastLedColor;  // color last written to the LED (all zero = off)
};
