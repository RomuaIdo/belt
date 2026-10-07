#pragma once

#include <Arduino.h>

#include "Domain/MasterLink.h"
#include "Domain/SampleBuffer.h"
#include "Input/Button.h"
#include "Network/EspNowTransceiver.h"
#include "Pairing/ChannelScanner.h"
#include "Pairing/PairingService.h"
#include "Protocol/Message.h"
#include "Sensor/Mpu6050.h"
#include "Storage/MasterLinkStorage.h"
#include "Util/AppConfig.h"

// Orchestrates slave subsystems and firmware lifecycle (setup/loop).
//
// Pairing, started by the pairing button:
//   1. scan channels 1..13 sending a broadcast PairRequest on each;
//   2. a master that hears it answers PairWait: lock that channel and master and
//      keep repeating the request (unicast) while the caregiver sets the belt up;
//   3. on PairAccept (caregiver pressed Save) save the master's MAC and channel,
//      then answer PairConfirm. Nothing is saved before that PairAccept.
//
// Alert: holding the alert button for kAlertHoldMs sends one Alert to the paired
// master. Without a paired master the press is ignored.
//
// IMU: the MPU-6050 is read every kImuSampleIntervalMs (100 Hz) and each sample goes
// into a circular buffer holding the last kImuBufferSamples (~3 s).
class AppController {
public:
    AppController();

    void setup();
    void loop();

private:
    void startPairing();
    void sendAlert();
    void processRadio();
    void processFrame(const RadioFrame& frame);
    void handlePairWait(const String& sender);
    void handlePairAccept(const String& sender, uint16_t seq, uint8_t channel);
    void sendMessage(const String& mac, Protocol::MessageType type, uint16_t seq);
    void restoreMasterChannel();
    void processImu();

    MasterLinkStorage masterLinkStorage;
    MasterLink masterLink;
    EspNowTransceiver espNow;
    Button pairingButton;
    Button alertButton;
    PairingService pairingService;
    ChannelScanner channelScanner;
    Mpu6050 mpu;
    SampleBuffer<AppConfig::kImuBufferSamples> imuBuffer;
    bool mpuReady = false;         // begin() succeeded; reads are skipped otherwise
    uint32_t lastImuReadMs = 0;    // last IMU read
    uint32_t lastImuPrintMs = 0;   // last debug print of the latest sample

    String lockedMaster;         // master that answered PairWait in this attempt
    uint32_t lastRequestMs = 0;    // last PairRequest sent while waiting
    uint32_t lastMasterSignalMs = 0;  // last PairWait heard while waiting
    uint16_t nextSeq = 1;

    uint32_t alertPressStartMs = 0;  // when the alert button was pressed
    bool alertSent = false;          // the current hold already sent its alert
};
