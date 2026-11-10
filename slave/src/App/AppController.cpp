#include "App/AppController.h"
#include "Util/AppConfig.h"
#include "Util/MacUtils.h"
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>

static_assert(Bmi270::kFifoMaxFrames <= FallDetector::kMaxFramesPerRead,
              "FallDetector must hold a whole FIFO read");

AppController::AppController()
    : masterLinkStorage(AppConfig::kMasterLinkFilePath),
      pairingButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      alertButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      pairingService(AppConfig::kPairingTimeoutMs, []() { return millis(); }),
      fallService(AppConfig::kFallCancelWindowMs, []() { return millis(); }),
      stateHandler(AppConfig::kStatusLedPin, AppConfig::kBuzzerPin, AppConfig::kVibrationPin),
      channelScanner(AppConfig::kPairingMinChannel, AppConfig::kPairingMaxChannel,
                     AppConfig::kPairingChannelDwellMs, []() { return millis(); }),
      imu(AppConfig::kImuSpiCsPin, AppConfig::kImuSpiHz) {}

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
    processImu();

    if (pairingButton.update(digitalRead(AppConfig::kPairingButtonPin), millis())) {
        Serial.println("AppController: pairing button pressed, looking for the master...");
        startPairing();
    }

    if (alertButton.update(digitalRead(AppConfig::kAlertButtonPin), millis())) {
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
}

SystemSnapshot AppController::snapshot() const {
    return {pairingService.getState(), fallService.getState()};
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
    if (result.fall) fallService.onFallDetected();

    if (now - lastMotionMs >= AppConfig::kImuIdleAfterStillMs) {
        imuActive = false;
        fallDetector.reset();  // the next movement starts from a fresh FIFO read
        Serial.println("AppController: still, waiting for movement");
    }
}

void AppController::startPairing() {
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
    Serial.println("AppController: sending alert to the master");
    sendMessage(masterLink.getMacAddress(), Protocol::MessageType::Alert, nextSeq++);
    stateHandler.notify(SystemEvent::AlertSent, millis());
}

void AppController::restoreMasterChannel() {
    if (!masterLink.hasChannel()) return;
    if (!espNow.setChannel(masterLink.getChannel())) {
        Serial.printf("AppController: could not tune to channel %u\n",
                      static_cast<unsigned>(masterLink.getChannel()));
    }
}

void AppController::sendMessage(const String& mac, Protocol::MessageType type, uint16_t seq) {
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
