#pragma once

#include <Arduino.h>

#include "Domain/MasterLink.h"
#include "Fall/FallDetector.h"
#include "Fall/FallService.h"
#include "Input/Button.h"
#include "Network/EspNowTransceiver.h"
#include "Pairing/ChannelScanner.h"
#include "Pairing/PairingService.h"
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
// Alert: one click on the alert button sends an Alert to the paired master. Without
// a paired master the click is ignored.
//
// Fall: the BMI270 fills its FIFO at 50 Hz. While idle only INT1 (any-motion) is
// watched; on movement the FIFO is read every kImuReadIntervalMs and handed to the
// FallDetector, until kImuIdleAfterStillMs pass without movement. A confirmed fall
// starts the FallService countdown: a click on the alert button cancels it (false
// positive), otherwise the Alert goes to the master when it ends.
//
// Feedback: the StateHandler shows the pairing/fall state on the LED, buzzer and
// vibration motor; this class only reports snapshot() each loop and notify()s the
// transitions (paired, timed out, alert sent/rejected, fall cancelled, boot fault).
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
    FallDetector fallDetector;
    fall::ImuFrame imuFrames[Bmi270::kFifoMaxFrames];  // one FIFO read
    bool radioReady = false;       // espNow.init() succeeded
    bool imuReady = false;         // begin() succeeded; reads are skipped otherwise
    bool imuActive = false;        // moving: the FIFO is being read
    uint32_t lastImuReadMs = 0;    // last FIFO read
    uint32_t lastMotionMs = 0;     // last any-motion seen

    String lockedMaster;         // master that answered PairWait in this attempt
    uint32_t lastRequestMs = 0;    // last PairRequest sent while waiting
    uint32_t lastMasterSignalMs = 0;  // last PairWait heard while waiting
    uint16_t nextSeq = 1;
};
