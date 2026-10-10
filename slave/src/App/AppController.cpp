#include "App/AppController.h"
#include "Util/AppConfig.h"
#include "Util/MacUtils.h"
#include <LittleFS.h>
#include <WiFi.h>

AppController::AppController()
    : masterLinkStorage(AppConfig::kMasterLinkFilePath),
      pairingButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      alertButton(AppConfig::kButtonDebounceMs, /*activeLow=*/true),
      pairingService(AppConfig::kPairingTimeoutMs, []() { return millis(); }),
      alertService(AppConfig::kAlertHoldMs, []() { return millis(); }),
      stateHandler(AppConfig::kStatusLedPin, AppConfig::kBuzzerPin, AppConfig::kVibrationPin),
      channelScanner(AppConfig::kPairingMinChannel, AppConfig::kPairingMaxChannel,
                     AppConfig::kPairingChannelDwellMs, []() { return millis(); }),
      mpu(Wire) {}

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

    Wire.begin(AppConfig::kImuSdaPin, AppConfig::kImuSclPin, AppConfig::kImuI2cHz);
    mpuReady = mpu.begin();
    if (!mpuReady) {
        Serial.println("AppController: MPU-6050 init failed");
    }

    masterLink = masterLinkStorage.load();
    if (masterLink.hasMac()) {
        Serial.printf("AppController: restored paired master %s (channel %u)\n",
                      masterLink.getMacAddress().c_str(), static_cast<unsigned>(masterLink.getChannel()));
        restoreMasterChannel();
    }

    if (!radioReady || !mpuReady) stateHandler.notify(SystemEvent::BootFault, millis());

    Serial.println("AppController: setup complete");
}

void AppController::loop() {
    processImu();

    if (pairingButton.update(digitalRead(AppConfig::kPairingButtonPin), millis())) {
        Serial.println("AppController: pairing button pressed, looking for the master...");
        startPairing();
    }

    const uint32_t buttonNow = millis();
    if (alertButton.update(digitalRead(AppConfig::kAlertButtonPin), buttonNow)) {
        Serial.println("AppController: Alert button pressed, looking for the master...");
        alertService.onPress();
    }
    if (alertService.update(alertButton.isPressed())) {
        sendAlert();  // once per hold
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
    return {pairingService.getState(), alertService.getState()};
}

void AppController::processImu() {
    if (!mpuReady) return;

    const uint32_t now = millis();
    if (now - lastImuReadMs < AppConfig::kImuSampleIntervalMs) return;
    lastImuReadMs = now;

    ImuSample sample;
    if (mpu.readSample(sample)) imuBuffer.push(sample);

    if (imuBuffer.size() > 0 && now - lastImuPrintMs >= AppConfig::kImuDebugPrintMs) {
        lastImuPrintMs = now;
        const ImuSample& s = imuBuffer.latest();
        // "\r" and fixed-width fields: each print overwrites the previous one in place.
        Serial.printf("\rAppController: IMU a=[%6.2f %6.2f %6.2f] g g=[%7.1f %7.1f %7.1f] dps buf=%3u | ",
                      Mpu6050::toG(s.ax), Mpu6050::toG(s.ay), Mpu6050::toG(s.az),
                      Mpu6050::toDps(s.gx), Mpu6050::toDps(s.gy), Mpu6050::toDps(s.gz),
                      static_cast<unsigned>(imuBuffer.size()));
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
        alertService.abort();
        stateHandler.notify(SystemEvent::AlertRejected, millis());
        return;
    }
    Serial.println("AppController: alert button held, sending alert to the master");
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
