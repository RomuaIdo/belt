#pragma once

#include <Arduino.h>

#include "Domain/MasterLink.h"
#include "Fall/FallDetector.h"
#include "Fall/FallService.h"
#include "Input/Button.h"
#include "Network/EspNowTransceiver.h"
#include "Pairing/ChannelScanner.h"
#include "Pairing/PairingService.h"
#include "Power/BatteryMonitor.h"
#include "Protocol/Message.h"
#include "Sensor/Bmi270.h"
#include "State/StateHandler.h"
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
// Alert: one click on the alert button sends an Alert to the paired master, resent
// with the same seq until its AlertAck arrives (or every retry is used). Without a
// paired master the click is ignored.
//
// Fall: the BMI270 fills its FIFO at 50 Hz. While idle only INT1 (any-motion) is
// watched; on movement the FIFO is read every kImuReadIntervalMs and handed to the
// FallDetector, until kImuIdleAfterStillMs pass without movement. A confirmed fall
// starts the FallService countdown: a click on the alert button cancels it (false
// positive), otherwise the Alert goes to the master when it ends.
//
// Feedback: the StateHandler shows the pairing/fall state on the LED, buzzer and
// vibration motor; this class only reports snapshot() each loop and notify()s the
// transitions (paired, timed out, alert sent/rejected/failed, fall cancelled, boot fault).
//
// Power: when nothing is happening (StateHandler idle, no pairing, no alert waiting
// for its ack, buttons released) the ESP goes into light sleep, as in
// prototypes/bmi270_fall_detect.cpp: until any-motion when still, until the next
// FIFO read when moving. Either button also wakes it.
//
// Battery: read through the divider on GPIO8 at boot, every kBatteryReadIntervalMs
// (a light-sleep timer wakes the belt for it), on every button click and on every
// confirmed fall. While it is low the state is BatteryLow (quiet, still sleeps) and
// every low reading plays the BatteryLow event (3 beeps).
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
    void wakeRadio();
    void processImu();
    void resendAlert();
    void handleAlertAck(const String& sender, uint16_t seq);
    void maybeSleep();
    void checkBattery();
    SystemSnapshot snapshot() const;

    MasterLinkStorage masterLinkStorage;
    MasterLink masterLink;
    EspNowTransceiver espNow;
    Button pairingButton;
    Button alertButton;
    PairingService pairingService;
    FallService fallService;
    StateHandler stateHandler;
    ChannelScanner channelScanner;
    Bmi270 imu;
    BatteryMonitor battery;
    FallDetector fallDetector;
    fall::ImuFrame imuFrames[Bmi270::kFifoMaxFrames];  // one FIFO read
    bool radioReady = false;       // espNow.init() succeeded
    bool imuReady = false;         // begin() succeeded; reads are skipped otherwise
    bool imuActive = false;        // moving: the FIFO is being read
    uint32_t lastImuReadMs = 0;    // last FIFO read
    uint32_t lastMotionMs = 0;     // last any-motion seen

    bool alertPending = false;     // Alert sent, no AlertAck yet
    uint16_t alertSeq = 0;         // seq of the pending Alert (resends reuse it)
    uint8_t alertAttempts = 0;
    uint32_t lastAlertSendMs = 0;

    String lockedMaster;         // master that answered PairWait in this attempt
    uint32_t lastRequestMs = 0;    // last PairRequest sent while waiting
    uint32_t lastMasterSignalMs = 0;  // last PairWait heard while waiting
    uint16_t nextSeq = 1;
};
