#include "State/StateHandler.h"
#include "Util/AppConfig.h"

void StateHandler::Channel::startEvent(const Pattern& pattern, uint32_t nowMs) {
    eventPlayer.start(pattern, nowMs);
    eventActive = true;
}

bool StateHandler::Channel::eventPlaying(uint32_t nowMs) const {
    return eventActive && !eventPlayer.finished(nowMs);
}

bool StateHandler::Channel::wantsOn(uint32_t nowMs) {
    if (eventActive && eventPlayer.finished(nowMs)) eventActive = false;
    return (eventActive ? eventPlayer : statePlayer).isOn(nowMs);
}

StateHandler::StateHandler(uint8_t ledPin, uint8_t buzzerPin, uint8_t vibrationPin)
    : ledPin(ledPin), buzzerPin(buzzerPin), vibrationPin(vibrationPin) {}

void StateHandler::begin() {
    if (buzzerPin != AppConfig::kNoPin) pinMode(buzzerPin, OUTPUT);
    if (vibrationPin != AppConfig::kNoPin) pinMode(vibrationPin, OUTPUT);
    writeLed(false, Rgb{});
    writeBuzzer(false);
    writeVibration(false);
}

void StateHandler::update(const SystemSnapshot& snapshot, uint32_t nowMs) {
    const SystemState next = resolveState(snapshot);
    if (next != current) {
        Serial.printf("StateHandler: %s -> %s\n", toString(current), toString(next));
        current = next;
        applyState(nowMs);
    } else if (isIdle(nowMs)) {
        return;  // nothing active and every output already off
    }
    render(nowMs);
}

void StateHandler::notify(SystemEvent event, uint32_t nowMs) {
    Serial.printf("StateHandler: event %s\n", toString(event));
    const Feedback feedback = feedbackFor(event);
    if (feedback.led.kind != Pattern::Kind::Off) {
        led.startEvent(feedback.led, nowMs);
        eventColor = feedback.color;
    }
    if (feedback.buzzer.kind != Pattern::Kind::Off) buzzer.startEvent(feedback.buzzer, nowMs);
    if (feedback.vibration.kind != Pattern::Kind::Off) vibration.startEvent(feedback.vibration, nowMs);
    render(nowMs);
}

// The `on` flags matter: an event whose last phase is ON (offMs = 0) has finished
// by the clock but still holds its pin high until the next render() turns it off.
bool StateHandler::isIdle(uint32_t nowMs) const {
    return isQuiet(current) && !led.eventPlaying(nowMs) && !buzzer.eventPlaying(nowMs) &&
           !vibration.eventPlaying(nowMs) && !led.on && !buzzer.on && !vibration.on;
}

void StateHandler::prepareForSleep() {
    if (led.on) writeLed(false, Rgb{});
    if (buzzer.on) writeBuzzer(false);
    if (vibration.on) writeVibration(false);
    // Light sleep keeps each GPIO at its last level: make sure both transistor
    // bases really sit low, whatever the flags say.
    if (buzzerPin != AppConfig::kNoPin) digitalWrite(buzzerPin, LOW);
    if (vibrationPin != AppConfig::kNoPin) digitalWrite(vibrationPin, LOW);
}

void StateHandler::applyState(uint32_t nowMs) {
    const Feedback feedback = feedbackFor(current);
    led.statePlayer.start(feedback.led, nowMs);
    stateColor = feedback.color;
    buzzer.statePlayer.start(feedback.buzzer, nowMs);
    vibration.statePlayer.start(feedback.vibration, nowMs);
}

void StateHandler::render(uint32_t nowMs) {
    const bool ledOn = led.wantsOn(nowMs);
    const Rgb ledColor = ledOn ? (led.eventActive ? eventColor : stateColor) : Rgb{};
    if (ledOn != led.on || ledColor != lastLedColor) writeLed(ledOn, ledColor);

    const bool buzzerOn = buzzer.wantsOn(nowMs);
    if (buzzerOn != buzzer.on) writeBuzzer(buzzerOn);

    const bool vibrationOn = vibration.wantsOn(nowMs);
    if (vibrationOn != vibration.on) writeVibration(vibrationOn);
}

void StateHandler::writeLed(bool on, const Rgb& color) {
    led.on = on;
    lastLedColor = color;
    if (ledPin != AppConfig::kNoPin) neopixelWrite(ledPin, color.r, color.g, color.b);
}

// LEDC driven directly instead of tone()/noTone(): those only post to a FreeRTOS
// task, so the belt could go into light sleep before the tone was really stopped,
// with the LEDC output frozen on the transistor. These calls take effect at once.
void StateHandler::writeBuzzer(bool on) {
    buzzer.on = on;
    if (buzzerPin == AppConfig::kNoPin) return;
    if (on) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttachChannel(buzzerPin, AppConfig::kBuzzerFreqHz, AppConfig::kBuzzerLedcResolutionBits,
                          AppConfig::kBuzzerLedcChannel);
        ledcWrite(buzzerPin, 1u << (AppConfig::kBuzzerLedcResolutionBits - 1));  // 50 % duty
#else
        ledcSetup(AppConfig::kBuzzerLedcChannel, AppConfig::kBuzzerFreqHz, AppConfig::kBuzzerLedcResolutionBits);
        ledcAttachPin(buzzerPin, AppConfig::kBuzzerLedcChannel);
        ledcWrite(AppConfig::kBuzzerLedcChannel, 1u << (AppConfig::kBuzzerLedcResolutionBits - 1));  // 50 % duty
#endif
    } else {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcDetach(buzzerPin);
#else
        ledcWrite(AppConfig::kBuzzerLedcChannel, 0);
        ledcDetachPin(buzzerPin);
#endif
        pinMode(buzzerPin, OUTPUT);  // back to a plain GPIO, held low
        digitalWrite(buzzerPin, LOW);
    }
}

void StateHandler::writeVibration(bool on) {
    vibration.on = on;
    if (vibrationPin != AppConfig::kNoPin) digitalWrite(vibrationPin, on ? HIGH : LOW);
}
