#include "App/AppController.h"
#include "Util/AppConfig.h"
#include "Util/MacUtils.h"
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

static_assert(Bmi270::kFifoMaxFrames <= FallDetector::kMaxFramesPerRead,
              "FallDetector must hold a whole FIFO read");

namespace {
// Average of a few ADC readings on the battery divider, in mV on the pin.
uint32_t readBatteryPinMv() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < AppConfig::kBatteryAdcSamples; i++) {
        sum += analogReadMilliVolts(AppConfig::kBatteryAdcPin);
    }
    return sum / AppConfig::kBatteryAdcSamples;
}
}  // namespace

AppController::AppController()
    : masterLinkStorage(AppConfig::kMasterLinkFilePath),
      pairingButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      alertButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      pairingService(AppConfig::kPairingTimeoutMs, []() { return millis(); }),
      fallService(AppConfig::kFallCancelWindowMs, []() { return millis(); }),
      stateHandler(AppConfig::kStatusLedPin, AppConfig::kBuzzerPin, AppConfig::kVibrationPin),
      channelScanner(AppConfig::kPairingMinChannel, AppConfig::kPairingMaxChannel,
                     AppConfig::kPairingChannelDwellMs, []() { return millis(); }),
      imu(AppConfig::kImuSpiCsPin, AppConfig::kImuSpiHz),
      battery(readBatteryPinMv, AppConfig::kBatteryDividerRatio, AppConfig::kBatteryLowMv,
              AppConfig::kBatteryRecoverMv, AppConfig::kBatteryReadIntervalMs, []() { return millis(); }) {}

void AppController::setup() {
    Serial.begin(115200);

    if (!LittleFS.begin(true)) {
        Serial.println("AppController: failed to mount LittleFS");
    }

    // ESP-NOW and channel changes need the Wi-Fi driver running; the belt never
    // joins a network.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    pinMode(AppConfig::kPairingButtonPin, INPUT_PULLUP);
    pinMode(AppConfig::kAlertButtonPin, INPUT_PULLUP);
    stateHandler.begin();

    analogSetPinAttenuation(AppConfig::kBatteryAdcPin, ADC_11db);
    checkBattery();

    radioReady = espNow.init();
    if (!radioReady) {
        Serial.println("AppController: ESP-NOW init failed");
    }

    SPI.begin(AppConfig::kImuSpiSckPin, AppConfig::kImuSpiMisoPin, AppConfig::kImuSpiMosiPin,
              AppConfig::kImuSpiCsPin);
    pinMode(AppConfig::kImuIntPin, INPUT_PULLDOWN);
    imuReady = imu.begin();
    if (!imuReady) {
        Serial.println("AppController: BMI270 init failed");
    }

    masterLink = masterLinkStorage.load();
    if (masterLink.hasMac()) {
        Serial.printf("AppController: restored paired master %s (channel %u)\n",
                      masterLink.getMacAddress().c_str(), static_cast<unsigned>(masterLink.getChannel()));
        restoreMasterChannel();
    }

    if (!radioReady || !imuReady) stateHandler.notify(SystemEvent::BootFault, millis());

    Serial.println("AppController: setup complete");
}

void AppController::loop() {
    if (battery.isDue()) checkBattery();  // periodic reading (the sleep timer wakes the belt for it)

    processImu();

    if (pairingButton.update(digitalRead(AppConfig::kPairingButtonPin), millis())) {
        checkBattery();  // before the radio starts sending
        Serial.println("AppController: pairing button pressed, looking for the master...");
        startPairing();
    }

    if (alertButton.update(digitalRead(AppConfig::kAlertButtonPin), millis())) {
        checkBattery();
        if (fallService.cancel()) {
            Serial.println("AppController: fall cancelled by the wearer, no alert sent");
            stateHandler.notify(SystemEvent::FallCancelled, millis());
        } else {
            Serial.println("AppController: alert button pressed");
            sendAlert();
        }
    }
    if (fallService.update()) {
        Serial.println("AppController: fall not cancelled in time");
        sendAlert();
    }
    resendAlert();

    if (pairingService.isSearching() && channelScanner.tick()) {
        espNow.setChannel(channelScanner.currentChannel());
        sendMessage(Protocol::kBroadcastMac, Protocol::MessageType::PairRequest, nextSeq++);
    }

    if (pairingService.isWaiting()) {
        const uint32_t now = millis();
        if (now - lastMasterSignalMs >= AppConfig::kMasterSilenceMs) {
            // The master stopped answering (switched channel, powered off): scan again.
            Serial.println("AppController: master went silent, scanning again");
            pairingService.resumeSearching();
            channelScanner.reset();
        } else if (now - lastRequestMs >= AppConfig::kPairingRequestIntervalMs) {
            // Keeps the belt listed on the page and the master informed.
            sendMessage(lockedMaster, Protocol::MessageType::PairRequest, nextSeq++);
            lastRequestMs = now;
        }
    }

    processRadio();

    pairingService.tick();
    if (pairingService.getState() == PairingService::State::TimedOut) {
        Serial.println("AppController: pairing attempt timed out");
        pairingService.reset();
        restoreMasterChannel();  // scanning left the radio on an arbitrary channel
        stateHandler.notify(SystemEvent::PairingTimedOut, millis());
    }

    stateHandler.update(snapshot(), millis());
    maybeSleep();
}

SystemSnapshot AppController::snapshot() const {
    return {pairingService.getState(), fallService.getState(), alertPending, battery.isLow()};
}

void AppController::processImu() {
    if (!imuReady) return;

    const uint32_t now = millis();
    if (!imuActive) {
        // Still: only any-motion (latched on INT1) matters. The FIFO keeps the last
        // ~3.4 s, so the frames before the movement are read right away.
        if (digitalRead(AppConfig::kImuIntPin) != HIGH) return;
        imuActive = true;
        lastMotionMs = now;
        lastImuReadMs = now - AppConfig::kImuReadIntervalMs;
        Serial.println("AppController: movement, checking for falls");
    }
    if (now - lastImuReadMs < AppConfig::kImuReadIntervalMs) return;
    lastImuReadMs = now;

    if (imu.takeMotion()) lastMotionMs = now;  // still moving since the last read

    bool lostFrames = false;
    const size_t n = imu.readFifo(imuFrames, lostFrames);
    const FallDetector::Result result = fallDetector.process(imuFrames, n, lostFrames);
    if (result.candidate) {
        const fall::Features& f = result.features;
        Serial.printf("AppController: fall candidate (peak %.2f g, tilt %.0f deg) -> model p=%.3f, %s\n", f.peakG,
                      f.tiltChangeDeg, result.probability, result.fall ? "FALL" : "rejected");
    }
    if (result.fall) {
        fallService.onFallDetected();
        checkBattery();
    }

    if (now - lastMotionMs >= AppConfig::kImuIdleAfterStillMs) {
        imuActive = false;
        fallDetector.reset();  // the next movement starts from a fresh FIFO read
        Serial.println("AppController: still, waiting for movement");
    }
}

void AppController::checkBattery() {
    const bool low = battery.measure();
    Serial.printf("AppController: battery %u mV%s\n", static_cast<unsigned>(battery.lastBatteryMv()),
                  low ? " (low)" : "");
    if (low) stateHandler.notify(SystemEvent::BatteryLow, millis());
}

void AppController::startPairing() {
    wakeRadio();
    lockedMaster = "";
    pairingService.startSearching();
    channelScanner.reset();
    espNow.setChannel(channelScanner.currentChannel());
    sendMessage(Protocol::kBroadcastMac, Protocol::MessageType::PairRequest, nextSeq++);
}

void AppController::sendAlert() {
    if (!masterLink.hasMac()) {
        Serial.println("AppController: alert ignored, no master configured");
        stateHandler.notify(SystemEvent::AlertRejected, millis());
        return;
    }
    if (alertPending) {
        Serial.println("AppController: an alert is already waiting for its ack");
        return;
    }
    Serial.println("AppController: sending alert to the master");
    alertPending = true;
    alertSeq = nextSeq++;
    alertAttempts = 1;
    lastAlertSendMs = millis();
    sendMessage(masterLink.getMacAddress(), Protocol::MessageType::Alert, alertSeq);
}

// Same seq on every resend: the master acknowledges it again without queuing it twice.
void AppController::resendAlert() {
    if (!alertPending || millis() - lastAlertSendMs < AppConfig::kAlertRetryIntervalMs) return;
    if (alertAttempts >= AppConfig::kAlertMaxAttempts) {
        Serial.println("AppController: no AlertAck from the master, giving up");
        alertPending = false;
        stateHandler.notify(SystemEvent::AlertFailed, millis());
        return;
    }
    alertAttempts++;
    lastAlertSendMs = millis();
    sendMessage(masterLink.getMacAddress(), Protocol::MessageType::Alert, alertSeq);
}

void AppController::handleAlertAck(const String& sender, uint16_t seq) {
    if (!alertPending || seq != alertSeq) return;  // stale or not ours
    if (!masterLink.hasMac() || !MacUtils::equal(sender, masterLink.getMacAddress())) return;
    Serial.printf("AppController: master acknowledged the alert (attempt %u)\n", static_cast<unsigned>(alertAttempts));
    alertPending = false;
    stateHandler.notify(SystemEvent::AlertSent, millis());
}

// Light sleep when nothing is happening, as in prototypes/bmi270_fall_detect.cpp.
// Still: until any-motion raises INT1. Moving: until the next FIFO read (INT1 stays
// latched while moving, so it cannot be a wake source then). Both buttons wake it,
// and a timer wakes it for the periodic battery reading.
void AppController::maybeSleep() {
    if (!AppConfig::kUseLightSleep) return;
    if (AppConfig::kStayAwakeOnUsb && HWCDC::isPlugged()) return;

    const uint32_t now = millis();
    if (!stateHandler.isIdle(now) || pairingService.isActive() || alertPending || fallService.isCountingDown()) return;
    if (digitalRead(AppConfig::kPairingButtonPin) == LOW || digitalRead(AppConfig::kAlertButtonPin) == LOW ||
        pairingButton.isPressed() || alertButton.isPressed()) {
        return;  // pressed, or the release is still being debounced
    }

    if (battery.isDue()) return;  // read it first
    uint32_t timerMs = battery.msUntilDue();
    bool wakeOnImu = false;
    if (imuReady && imuActive) {
        const uint32_t elapsed = now - lastImuReadMs;
        if (elapsed >= AppConfig::kImuReadIntervalMs) return;  // a read is due
        timerMs = min(timerMs, AppConfig::kImuReadIntervalMs - elapsed);
    } else if (imuReady) {
        if (digitalRead(AppConfig::kImuIntPin) == HIGH) return;  // movement to handle first
        wakeOnImu = true;
    }

    const gpio_num_t pairingPin = static_cast<gpio_num_t>(AppConfig::kPairingButtonPin);
    const gpio_num_t alertPin = static_cast<gpio_num_t>(AppConfig::kAlertButtonPin);
    const gpio_num_t imuPin = static_cast<gpio_num_t>(AppConfig::kImuIntPin);

    // ESP-IDF requires Wi-Fi stopped before light sleep. It stays off after waking
    // until something has to be sent (wakeRadio()), so the 1 s FIFO reads while
    // moving do not restart it.
    if (radioReady) espNow.suspend();

    Serial.flush();
    gpio_wakeup_enable(pairingPin, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(alertPin, GPIO_INTR_LOW_LEVEL);
    if (wakeOnImu) gpio_wakeup_enable(imuPin, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    if (timerMs > 0) esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timerMs) * 1000);

    esp_light_sleep_start();

    // Back to no interrupt type: left at a level type, a pin still at that level
    // would keep firing the GPIO interrupt and trip the interrupt watchdog.
    gpio_wakeup_disable(pairingPin);
    gpio_wakeup_disable(alertPin);
    if (wakeOnImu) gpio_wakeup_disable(imuPin);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (timerMs > 0) esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

void AppController::restoreMasterChannel() {
    if (!masterLink.hasChannel()) return;
    if (!espNow.setChannel(masterLink.getChannel())) {
        Serial.printf("AppController: could not tune to channel %u\n",
                      static_cast<unsigned>(masterLink.getChannel()));
    }
}

// Turns the radio back on after light sleep, on the paired master's channel.
void AppController::wakeRadio() {
    if (!radioReady || !espNow.isSuspended()) return;
    if (!espNow.resume()) {
        Serial.println("AppController: could not restart the radio");
        return;
    }
    restoreMasterChannel();
}

void AppController::sendMessage(const String& mac, Protocol::MessageType type, uint16_t seq) {
    wakeRadio();
    uint8_t frame[Protocol::kMaxFrameSize];
    const size_t size = Protocol::buildFrame(frame, sizeof(frame), type, seq);
    espNow.send(mac, frame, size);
}

void AppController::processRadio() {
    RadioFrame frame;
    while (espNow.receive(frame)) {
        processFrame(frame);
    }
}

void AppController::processFrame(const RadioFrame& frame) {
    // Anything without our magic, a known type and the exact size is dropped.
    Protocol::Header header;
    if (!Protocol::parseFrame(frame.data, frame.len, header)) return;

    const String sender = MacUtils::format(frame.mac);
    switch (header.type) {
        case Protocol::MessageType::PairWait:
            handlePairWait(sender);
            break;
        case Protocol::MessageType::PairAccept:
            handlePairAccept(sender, header.seq, frame.data[Protocol::kPairAcceptChannelOffset]);
            break;
        case Protocol::MessageType::AlertAck:
            handleAlertAck(sender, header.seq);
            break;
        default:
            break;  // PairRequest, PairConfirm and Alert are never addressed to a belt
    }
}

void AppController::handlePairWait(const String& sender) {
    if (!pairingService.isActive()) return;  // nobody asked

    if (pairingService.isSearching()) {
        // First master to answer: stay on this channel and talk only to it.
        lockedMaster = sender;
        pairingService.onPairWait();
        lastRequestMs = millis();
        Serial.printf("AppController: master %s heard us, waiting for the caregiver to set up this belt\n",
                      sender.c_str());
    } else if (!MacUtils::equal(sender, lockedMaster)) {
        return;  // another master; ignore
    }
    lastMasterSignalMs = millis();
}

void AppController::handlePairAccept(const String& sender, uint16_t seq, uint8_t channel) {
    const bool pairing = pairingService.isActive();
    const bool fromPairedMaster =
        masterLink.hasMac() && MacUtils::equal(masterLink.getMacAddress(), sender);

    if (pairing) {
        if (pairingService.isWaiting() && !MacUtils::equal(sender, lockedMaster)) return;
    } else if (!fromPairedMaster) {
        return;  // not pairing: only the master we already trust may repeat its accept
    }
    if (channel < AppConfig::kPairingMinChannel || channel > AppConfig::kPairingMaxChannel) return;

    if (pairing || channel != masterLink.getChannel()) {
        // Save first and confirm only if it worked: a PairConfirm makes the master
        // register this belt, so it must be true that this belt can reach it.
        const MasterLink link(sender, channel);
        if (!masterLinkStorage.save(link)) {
            Serial.println("AppController: failed to save the master link; not confirming");
            return;
        }
        masterLink = link;
        espNow.setChannel(channel);
        pairingService.onPairAccepted();
        Serial.printf("AppController: paired with master %s (channel %u)\n", sender.c_str(),
                      static_cast<unsigned>(channel));
        stateHandler.notify(SystemEvent::PairingSucceeded, millis());
    }

    // Answer every PairAccept: the master resends it until it hears this.
    sendMessage(sender, Protocol::MessageType::PairConfirm, seq);
}
