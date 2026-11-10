// Unit tests for the slave StateHandler (all pins kNoPin: no hardware is touched).
// Run: pio test -e esp32-s3-supermini-test -f test_state_handler

#include <Arduino.h>
#include <unity.h>

#include "State/StateHandler.h"
#include "Util/AppConfig.h"

namespace {
using Pairing = PairingService::State;
using Fall = FallService::State;

int asInt(SystemState state) { return static_cast<int>(state); }

StateHandler makeHandler() {
    StateHandler handler(AppConfig::kNoPin, AppConfig::kNoPin, AppConfig::kNoPin);
    handler.begin();
    return handler;
}

SystemSnapshot snap(Pairing pairing, Fall fall = Fall::Idle) {
    SystemSnapshot snapshot;
    snapshot.pairing = pairing;
    snapshot.fall = fall;
    return snapshot;
}
}  // namespace

void test_resolve_state_priority() {
    TEST_ASSERT_EQUAL(asInt(SystemState::Idle), asInt(resolveState(snap(Pairing::Idle))));
    TEST_ASSERT_EQUAL(asInt(SystemState::Idle), asInt(resolveState(snap(Pairing::Paired))));
    TEST_ASSERT_EQUAL(asInt(SystemState::Idle), asInt(resolveState(snap(Pairing::TimedOut))));
    TEST_ASSERT_EQUAL(asInt(SystemState::PairingSearching), asInt(resolveState(snap(Pairing::Searching))));
    TEST_ASSERT_EQUAL(asInt(SystemState::PairingWaiting), asInt(resolveState(snap(Pairing::Waiting))));
    TEST_ASSERT_EQUAL(asInt(SystemState::FallCountdown), asInt(resolveState(snap(Pairing::Idle, Fall::Countdown))));
    TEST_ASSERT_EQUAL(asInt(SystemState::FallCountdown),
                      asInt(resolveState(snap(Pairing::Searching, Fall::Countdown))));
}

void test_idle_does_nothing() {
    StateHandler handler = makeHandler();

    handler.update(snap(Pairing::Idle), 0);
    handler.update(snap(Pairing::Idle), 10000);

    TEST_ASSERT_TRUE(handler.isIdle(10000));
    TEST_ASSERT_FALSE(handler.ledIsOn());
    TEST_ASSERT_FALSE(handler.buzzerIsOn());
    TEST_ASSERT_FALSE(handler.vibrationIsOn());
}

void test_pairing_blinks_the_led_until_it_ends() {
    StateHandler handler = makeHandler();

    handler.update(snap(Pairing::Searching), 1000);
    TEST_ASSERT_EQUAL(asInt(SystemState::PairingSearching), asInt(handler.state()));
    TEST_ASSERT_FALSE(handler.isIdle(1000));
    TEST_ASSERT_TRUE(handler.ledIsOn());

    handler.update(snap(Pairing::Searching), 1150);  // off half of the 150/150 blink
    TEST_ASSERT_FALSE(handler.ledIsOn());
    handler.update(snap(Pairing::Searching), 1300);
    TEST_ASSERT_TRUE(handler.ledIsOn());

    handler.update(snap(Pairing::Idle), 2000);  // pairing over
    TEST_ASSERT_EQUAL(asInt(SystemState::Idle), asInt(handler.state()));
    TEST_ASSERT_FALSE(handler.ledIsOn());
    TEST_ASSERT_TRUE(handler.isIdle(2000));
}

void test_fall_overrides_pairing_and_pairing_resumes() {
    StateHandler handler = makeHandler();

    handler.update(snap(Pairing::Waiting), 0);
    handler.update(snap(Pairing::Waiting, Fall::Countdown), 100);
    TEST_ASSERT_EQUAL(asInt(SystemState::FallCountdown), asInt(handler.state()));
    TEST_ASSERT_TRUE(handler.ledIsOn());        // every output starts on
    TEST_ASSERT_TRUE(handler.buzzerIsOn());
    TEST_ASSERT_TRUE(handler.vibrationIsOn());

    handler.update(snap(Pairing::Waiting), 200);  // cancelled or sent
    TEST_ASSERT_EQUAL(asInt(SystemState::PairingWaiting), asInt(handler.state()));
    TEST_ASSERT_FALSE(handler.buzzerIsOn());
    TEST_ASSERT_FALSE(handler.vibrationIsOn());
}

void test_fall_countdown_keeps_going_until_it_ends() {
    StateHandler handler = makeHandler();

    handler.update(snap(Pairing::Idle, Fall::Countdown), 0);
    handler.update(snap(Pairing::Idle, Fall::Countdown), 300);  // buzzer 200/800: off
    TEST_ASSERT_FALSE(handler.buzzerIsOn());
    handler.update(snap(Pairing::Idle, Fall::Countdown), 9000);  // repeats while the state lasts
    TEST_ASSERT_TRUE(handler.buzzerIsOn());
    TEST_ASSERT_FALSE(handler.isIdle(9000));
}

void test_fall_cancelled_flashes_green_then_goes_idle() {
    StateHandler handler = makeHandler();

    handler.update(snap(Pairing::Idle, Fall::Countdown), 0);
    handler.update(snap(Pairing::Idle), 100);
    handler.notify(SystemEvent::FallCancelled, 100);  // LED 400 ms
    TEST_ASSERT_TRUE(handler.ledIsOn());
    TEST_ASSERT_FALSE(handler.buzzerIsOn());

    handler.update(snap(Pairing::Idle), 500);
    TEST_ASSERT_FALSE(handler.ledIsOn());
    TEST_ASSERT_TRUE(handler.isIdle(500));
}

void test_event_plays_then_ends_on_its_own() {
    StateHandler handler = makeHandler();

    handler.notify(SystemEvent::PairingSucceeded, 1000);  // buzzer 2x 80/80, LED 400 ms
    TEST_ASSERT_FALSE(handler.isIdle(1000));
    TEST_ASSERT_TRUE(handler.buzzerIsOn());
    TEST_ASSERT_TRUE(handler.ledIsOn());
    TEST_ASSERT_FALSE(handler.vibrationIsOn());  // the event does not use it

    handler.update(snap(Pairing::Idle), 1100);  // buzzer between beeps
    TEST_ASSERT_FALSE(handler.buzzerIsOn());
    TEST_ASSERT_TRUE(handler.ledIsOn());

    handler.update(snap(Pairing::Idle), 1330);  // buzzer finished (2 * 160 ms)
    TEST_ASSERT_FALSE(handler.buzzerIsOn());
    TEST_ASSERT_FALSE(handler.isIdle(1330));    // LED pulse still running

    handler.update(snap(Pairing::Idle), 1400);  // LED finished (400 ms)
    TEST_ASSERT_FALSE(handler.ledIsOn());
    TEST_ASSERT_TRUE(handler.isIdle(1400));
}

void test_event_does_not_touch_outputs_it_does_not_use() {
    StateHandler handler = makeHandler();

    handler.update(snap(Pairing::Searching), 0);  // LED blinking
    handler.notify(SystemEvent::AlertRejected, 10);  // vibration only
    TEST_ASSERT_TRUE(handler.vibrationIsOn());
    TEST_ASSERT_TRUE(handler.ledIsOn());          // keeps following the state
}

void test_output_returns_to_the_state_after_an_event() {
    StateHandler handler = makeHandler();

    handler.update(snap(Pairing::Waiting), 0);   // LED 500/500, on until 500
    handler.notify(SystemEvent::BootFault, 0);    // LED 3x 100/100
    handler.update(snap(Pairing::Waiting), 150);
    TEST_ASSERT_FALSE(handler.ledIsOn());         // event's off phase

    handler.update(snap(Pairing::Waiting), 1100); // event over, state's on phase (1000-1500)
    TEST_ASSERT_TRUE(handler.ledIsOn());          // back to the state
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_resolve_state_priority);
    RUN_TEST(test_idle_does_nothing);
    RUN_TEST(test_pairing_blinks_the_led_until_it_ends);
    RUN_TEST(test_fall_overrides_pairing_and_pairing_resumes);
    RUN_TEST(test_fall_countdown_keeps_going_until_it_ends);
    RUN_TEST(test_fall_cancelled_flashes_green_then_goes_idle);
    RUN_TEST(test_event_plays_then_ends_on_its_own);
    RUN_TEST(test_event_does_not_touch_outputs_it_does_not_use);
    RUN_TEST(test_output_returns_to_the_state_after_an_event);
    UNITY_END();
}

void loop() {}
