// BMI270 (SPI) fall-detection flow prototype.
//
// Flow:
//   1. BMI270 samples accel + gyro at 50 Hz into its 2 KB FIFO (headerless,
//      12 B/frame -> ~170 frames = ~3.4 s; oldest frames are dropped when full).
//   2. Idle: the ESP sits in light sleep until any-motion raises INT1
//      (latched) and the GPIO wakes it. It reads the FIFO at once (the frames
//      before the movement are already there) and switches to active.
//   3. Active: light sleep on a timer, waking every kActiveReadIntervalMs to
//      read the FIFO and append it to a history. The ESP is awake only for
//      the read and the checks (~15 ms), not while the FIFO fills. After
//      kIdleAfterStillMs without any-motion it goes back to idle.
//   4. Every 2 s window (100 frames) ending in the frames just read is checked
//      by a cheap heuristic, stepping kWindowStrideFrames. The passing window
//      with the strongest impact goes to a small CNN that confirms the fall.
//   Heuristic, model and their SisFall results: fall_pipeline.h and
//   fall_model/README.md.
//
// Why the heuristic: any-motion fires when the accel moves more than a
// threshold away from a reference sample (updated on each detection,
// datasheet 4.8.2). The threshold tops out at ~1 g (11 bits, 0.49 mg/LSB) and the BMI270 has
// no high-g feature. So any-motion wakes on ordinary movement too; the ESP has
// to filter. The heuristic is a recall-first gate: the model removes the
// false candidates, a fall it drops is lost for good.
//
// Boot check: keep the board STILL for ~5 s after reset. The prototype lets
// the FIFO overflow and checks that every frame still reads ~1 g (i.e. the
// BMI270 drops whole frames, unlike the MPU-6050 FIFO).
//
// Wiring (BMI270 breakout -> ESP32-S3 Super Mini):
//   VCC/VIN -> 3V3   GND -> GND
//   SCL/SCK -> GPIO12   SDA/SDI -> GPIO11 (MOSI)   SDO -> GPIO13 (MISO)
//   CS/CSB  -> GPIO10   INT1 -> GPIO7   INT2 -> not connected
// The BMI270 starts in I2C mode and switches to SPI on the first CS rising edge.
// Build/flash: cd slave && pio run -e proto-bmi270-fall -t upload -t monitor
// While debugging over USB set kUseLightSleep = false: the S3 USB serial drops
// during light sleep and the monitor may lose the log.

#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "SparkFun_BMI270_Arduino_Library.h"
#include "fall_pipeline.h"

namespace {

// Pins (ESP32-S3 Super Mini). GPIO10-13 are the S3 FSPI IOMUX pins.
constexpr uint8_t kSpiCsPin = 10;
constexpr uint8_t kSpiMosiPin = 11;
constexpr uint8_t kSpiSckPin = 12;
constexpr uint8_t kSpiMisoPin = 13;
constexpr uint8_t kImuInt1Pin = 7;
constexpr uint32_t kSpiHz = 4000000;  // BMI270 allows 10 MHz; 4 MHz is safe on jumper wires

constexpr bool kUseLightSleep = true;
// Debug: onboard WS2812 shows the mode, since the USB serial drops in light
// sleep and the log can be lost. The LED keeps its color while the ESP sleeps.
// Off = idle, blue = active (reading every second), red = a fall was confirmed
// (stays until reset). Costs ~1 mA while lit: turn off to measure power.
constexpr bool kStatusLed = true;
constexpr uint8_t kStatusLedPin = 48;
constexpr uint8_t kLedLevel = 16;
constexpr bool kDumpWindowCsv = true;  // print candidate windows as CSV (model training data)

constexpr uint32_t kSampleHz = fall::kSampleHz;
constexpr size_t kWindowFrames = fall::kWindowFrames;  // 2 s
constexpr size_t kFifoBytes = 2048;
constexpr size_t kFrameBytes = 12;  // headerless accel + gyro
constexpr size_t kFifoMaxFrames = kFifoBytes / kFrameBytes;
// A full FIFO read back 168 frames on the board: it counts as full two frames
// before the 2048 B end (datasheet 4.7.5) and then drops the oldest frames.
constexpr size_t kFifoFullFrames = kFifoMaxFrames - 4;
// Active mode. On SisFall (simulated, test subjects) this keeps 100% of falls and
// 1.4% of ADL recordings, as the old "stay awake 1 s after any-motion" scheme
// did, while the ESP is awake ~1.4% of the time during movement instead of ~52%.
constexpr uint32_t kActiveReadIntervalMs = 1000;  // FIFO holds ~3.4 s, so no frame is lost
// Must exceed the 2 s window: after a fall the person lies still, and the
// windows ending up to 2 s after the impact still have to be checked.
constexpr uint32_t kIdleAfterStillMs = 5000;

// any-motion: duration in 20 ms steps, threshold in 0.49 mg steps (max 2047 ~ 1 g).
constexpr uint16_t kAnyMotionDuration = 1;     // 20 ms
constexpr uint16_t kAnyMotionThreshold = 512;  // ~0.25 g

constexpr size_t kWindowStrideFrames = 5;  // check a window every 0.1 s of new data

// Raw registers, read directly (see readFifo()).
constexpr uint8_t kRegFifoLength0 = 0x24;
constexpr uint8_t kRegFifoData = 0x26;
constexpr int16_t kFifoOverRead = -32768;  // 0x8000: no data at this position
constexpr float kAccelLsbPerG = 2048.0f;   // +-16 g
constexpr float kGyroLsbPerDps = 16.384f;  // +-2000 dps

constexpr float kStillMinG = 0.85f;
constexpr float kStillMaxG = 1.15f;

BMI270 imu;
uint8_t fifoRaw[kFifoBytes];
fall::ImuFrame fifo[kFifoMaxFrames];
// Frames already read, kept so back-to-back wakes still get a full window:
// reading the FIFO drains it. Light sleep keeps RAM.
fall::ImuFrame history[kWindowFrames + kFifoMaxFrames];
size_t historyCount = 0;
volatile bool imuIntFired = false;

void IRAM_ATTR onImuInt() { imuIntFired = true; }

bool check(const char* what, int8_t err) {
    if (err != BMI2_OK) Serial.printf("BMI270: %s failed (%d)\n", what, err);
    return err == BMI2_OK;
}

bool setupImu() {
    if (!check("beginSPI", imu.beginSPI(kSpiCsPin, kSpiHz, SPI))) return false;

    // Accel +-16 g and gyro +-2000 dps so an impact does not saturate; both at 50 Hz.
    bmi2_sens_config cfg[2];
    cfg[0].type = BMI2_ACCEL;
    cfg[1].type = BMI2_GYRO;
    if (!check("getConfigs", imu.getConfigs(cfg, 2))) return false;
    cfg[0].cfg.acc.odr = BMI2_ACC_ODR_50HZ;
    cfg[0].cfg.acc.range = BMI2_ACC_RANGE_16G;
    cfg[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    cfg[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    cfg[1].cfg.gyr.odr = BMI2_GYR_ODR_50HZ;
    cfg[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    cfg[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    cfg[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    if (!check("setConfigs", imu.setConfigs(cfg, 2))) return false;

    // Keeps register access simple; revisit for power once the flow works.
    if (!check("disableAdvancedPowerSave", imu.disableAdvancedPowerSave())) return false;

    BMI270_FIFOConfig fifoCfg;
    fifoCfg.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN;  // headerless, stream mode (oldest dropped)
    fifoCfg.watermark = 0;
    fifoCfg.accelDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
    fifoCfg.gyroDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
    fifoCfg.accelFilter = BMI2_ENABLE;
    fifoCfg.gyroFilter = BMI2_ENABLE;
    fifoCfg.selfWakeUp = BMI2_ENABLE;
    if (!check("setFIFOConfig", imu.setFIFOConfig(fifoCfg))) return false;

    if (!check("enableFeature(ANY_MOTION)", imu.enableFeature(BMI2_ANY_MOTION))) return false;
    bmi2_sens_config motion;
    motion.type = BMI2_ANY_MOTION;
    motion.cfg.any_motion.duration = kAnyMotionDuration;
    motion.cfg.any_motion.threshold = kAnyMotionThreshold;
    motion.cfg.any_motion.select_x = BMI2_ENABLE;
    motion.cfg.any_motion.select_y = BMI2_ENABLE;
    motion.cfg.any_motion.select_z = BMI2_ENABLE;
    if (!check("setConfig(ANY_MOTION)", imu.setConfig(motion))) return false;
    if (!check("mapInterruptToPin", imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1))) return false;

    // Latched: light-sleep GPIO wake is level based, a short pulse could be
    // missed. Reading the interrupt status clears the latch.
    bmi2_int_pin_config pin;
    pin.pin_type = BMI2_INT1;
    pin.int_latch = BMI2_INT_LATCH;
    pin.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    pin.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    pin.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    pin.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    if (!check("setInterruptPinConfig", imu.setInterruptPinConfig(pin))) return false;

    return check("flushFIFO", imu.flushFIFO());
}

uint16_t clearImuInterrupt() {
    uint16_t status = 0;
    imu.getInterruptStatus(&status);
    imuIntFired = false;
    return status;
}

void spiRead(uint8_t reg, uint8_t* buf, size_t len) {
    memset(buf, 0, len);
    SPI.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
    digitalWrite(kSpiCsPin, LOW);
    SPI.transfer(reg | 0x80);
    SPI.transfer(0);  // BMI270 SPI reads start with one dummy byte
    SPI.transfer(buf, len);
    digitalWrite(kSpiCsPin, HIGH);
    SPI.endTransaction();
}

int16_t le16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }

// Reads the whole FIFO; returns the frame count (oldest first).
// Not imu.getFIFOData(): over SPI it mallocs the FIFO length but reads one
// extra (dummy) byte into it, corrupting the heap (SparkFun lib 1.0.3).
uint16_t readFifo() {
    uint8_t lenBuf[2];
    spiRead(kRegFifoLength0, lenBuf, sizeof(lenBuf));
    const size_t len = min(static_cast<size_t>((lenBuf[0] | (lenBuf[1] << 8)) & 0x3FFF), kFifoBytes);
    spiRead(kRegFifoData, fifoRaw, len);
    // Should not happen on the BMI270 (whole frames are dropped on overflow,
    // checked at boot), but a misaligned read would turn every frame into
    // garbage: drop it and start over instead of feeding the model.
    if (len % kFrameBytes != 0) {
        Serial.printf("  FIFO length %u is not a multiple of %u: flushing\n", static_cast<unsigned>(len),
                      static_cast<unsigned>(kFrameBytes));
        imu.flushFIFO();
        historyCount = 0;
        return 0;
    }

    // Headerless frame with accel + gyro: GYR x,y,z then ACC x,y,z, little endian.
    uint16_t n = 0;
    for (size_t off = 0; off + kFrameBytes <= len && n < kFifoMaxFrames; off += kFrameBytes) {
        const uint8_t* f = fifoRaw + off;
        if (le16(f + 6) == kFifoOverRead) break;  // read past the stored data
        fall::ImuFrame& s = fifo[n++];
        s.gx = le16(f) / kGyroLsbPerDps;
        s.gy = le16(f + 2) / kGyroLsbPerDps;
        s.gz = le16(f + 4) / kGyroLsbPerDps;
        s.ax = le16(f + 6) / kAccelLsbPerG;
        s.ay = le16(f + 8) / kAccelLsbPerG;
        s.az = le16(f + 10) / kAccelLsbPerG;
    }
    return n;
}

// With the board still, every frame must read ~1 g. A misaligned FIFO
// (as on the MPU-6050) would show up as garbage magnitudes.
void checkFifoOverflowAlignment() {
    const uint32_t fillMs = kFifoMaxFrames * 1000 / kSampleHz + 1000;
    Serial.printf("FIFO overflow check: keep the board still for %lu ms...\n", static_cast<unsigned long>(fillMs));
    imu.flushFIFO();
    delay(fillMs);
    const uint16_t n = readFifo();
    uint16_t still = 0;
    float minG = 99.0f, maxG = 0.0f;
    for (uint16_t i = 0; i < n; i++) {
        const float g = fall::accelG(fifo[i]);
        minG = min(minG, g);
        maxG = max(maxG, g);
        if (g >= kStillMinG && g <= kStillMaxG) still++;
    }
    Serial.printf("  %u frames after overflow, %u read ~1 g (|a| %.2f..%.2f g) -> %s\n", n, still, minG, maxG,
                  (n > 0 && still == n) ? "ALIGNED" : "CHECK (board moving or FIFO misaligned)");
    clearImuInterrupt();
}

void dumpWindowCsv(const fall::ImuFrame* w, size_t n) {
    Serial.println("csv:i,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps");
    for (size_t i = 0; i < n; i++) {
        Serial.printf("csv:%u,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n", static_cast<unsigned>(i), w[i].ax, w[i].ay, w[i].az,
                      w[i].gx, w[i].gy, w[i].gz);
    }
}

// Appends the frames just read after the ones kept from earlier reads.
// Returns the total in `history`; `firstNew` is the index of the first new one.
size_t appendToHistory(uint16_t n, size_t& firstNew) {
    // A full FIFO has dropped its oldest frames, so the kept frames are no
    // longer contiguous with the new ones.
    if (n >= kFifoFullFrames) historyCount = 0;
    memcpy(history + historyCount, fifo, n * sizeof(fifo[0]));
    firstNew = historyCount;
    return historyCount + n;
}

// Keeps the newest kWindowFrames so the next read can still form windows
// ending in its first frames.
void trimHistory(size_t total) {
    const size_t keep = min(total, kWindowFrames);
    memmove(history, history + (total - keep), keep * sizeof(history[0]));
    historyCount = keep;
}

bool fallSeen = false;

void statusLed(uint8_t r, uint8_t g, uint8_t b) {
    if (kStatusLed && !fallSeen) neopixelWrite(kStatusLedPin, r, g, b);
}

void printFeatures(const fall::Features& f) {
    Serial.printf("  peak %.2f g @%u (%.2f s) | tilt %.0f deg | min before %.2f g | gyro near impact %.0f dps\n",
                  f.peakG, static_cast<unsigned>(f.peakIdx), f.peakIdx / static_cast<float>(kSampleHz),
                  f.tiltChangeDeg, f.minGBeforePeak, f.peakDpsNearImpact);
}

// Reads the FIFO and checks every window ending in the new frames.
void processFifo() {
    size_t firstNew = 0;
    const size_t total = appendToHistory(readFifo(), firstNew);
    if (total < kWindowFrames) {
        Serial.printf("  %u frames so far (need %u), skipping\n", static_cast<unsigned>(total),
                      static_cast<unsigned>(kWindowFrames));
        trimHistory(total);
        return;
    }

    // Every window ending in the new frames, newest first. A candidate is the
    // passing window with the strongest impact.
    const size_t lastEnd = max(kWindowFrames, firstNew + 1);
    size_t checked = 0, bestEnd = 0;
    fall::Features best{}, newest{};
    for (size_t end = total; end >= lastEnd; end -= kWindowStrideFrames) {
        const fall::Features f = fall::extractFeatures(history + (end - kWindowFrames), kWindowFrames);
        if (end == total) newest = f;
        if (fall::looksLikeFall(f) && f.peakG > best.peakG) {
            best = f;
            bestEnd = end;
        }
        checked++;
        if (end < lastEnd + kWindowStrideFrames) break;  // size_t: do not wrap below lastEnd
    }
    Serial.printf("  %u new frames, %u windows checked\n", static_cast<unsigned>(total - firstNew),
                  static_cast<unsigned>(checked));

    const bool candidate = bestEnd != 0;
    const fall::ImuFrame* window = history + ((candidate ? bestEnd : total) - kWindowFrames);
    printFeatures(candidate ? best : newest);
    if (candidate) {
        const uint32_t m0 = micros();
        const float p = fall::fallProbability(window);
        const uint32_t modelUs = micros() - m0;
        if (fall::modelConfirmsFall(p)) {
            Serial.printf("  -> FALL CONFIRMED by the model (p=%.3f, %lu us)\n", p, static_cast<unsigned long>(modelUs));
            if (kStatusLed) neopixelWrite(kStatusLedPin, kLedLevel, 0, 0);
            fallSeen = true;
            // TODO: raise the alert (ESP-NOW Alert to the master) once integrated.
        } else {
            Serial.printf("  -> candidate rejected by the model (p=%.3f, %lu us)\n", p,
                          static_cast<unsigned long>(modelUs));
        }
    } else {
        Serial.println("  -> not a fall (heuristic)");
    }
    // Only candidates: the model never sees the other windows.
    if (kDumpWindowCsv && candidate) dumpWindowCsv(window, kWindowFrames);
    trimHistory(total);
}

// Idle: light sleep until any-motion raises INT1.
void sleepUntilMotion() {
    clearImuInterrupt();  // the latch must be low or the level wake fires at once
    Serial.flush();
    if (kUseLightSleep) {
        gpio_wakeup_enable(static_cast<gpio_num_t>(kImuInt1Pin), GPIO_INTR_HIGH_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_light_sleep_start();
        // Back to no interrupt type: left at HIGH_LEVEL, the latched INT1 would
        // keep firing the GPIO interrupt and trip the interrupt watchdog.
        gpio_wakeup_disable(static_cast<gpio_num_t>(kImuInt1Pin));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    } else {
        while (!imuIntFired) delay(5);
    }
}

// Active: light sleep on a timer only. INT1 stays latched while the person
// moves, so GPIO wake is off here; the any-motion status is read on each wake.
void sleepFor(uint32_t ms) {
    if (ms == 0) return;  // the last read took the whole interval
    Serial.flush();
    if (kUseLightSleep) {
        esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000);
        esp_light_sleep_start();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    } else {
        delay(ms);
    }
}

enum class Mode { Idle, Active };
Mode mode = Mode::Idle;
uint32_t lastMotionMs = 0;
uint32_t lastReadMs = 0;

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(3000);  // USB CDC needs time to enumerate
    Serial.println("\n===== BMI270 fall-detection prototype =====");

    SPI.begin(kSpiSckPin, kSpiMisoPin, kSpiMosiPin, kSpiCsPin);
    pinMode(kImuInt1Pin, INPUT_PULLDOWN);
    // The ISR is only for busy-wait mode. In light sleep mode gpio_wakeup_enable()
    // turns the pin into a HIGH_LEVEL interrupt, and with an ISR attached the
    // latched INT1 retriggers it nonstop after wake (interrupt watchdog panic).
    if (!kUseLightSleep) attachInterrupt(digitalPinToInterrupt(kImuInt1Pin), onImuInt, RISING);

    while (!setupImu()) {
        Serial.println("BMI270 init failed, check wiring. Retrying...");
        delay(2000);
    }
    Serial.println("BMI270 ready (SPI, 50 Hz, +-16 g, +-2000 dps, FIFO accel+gyro)");

    checkFifoOverflowAlignment();
    statusLed(0, 0, 0);
    Serial.printf("Idle (%s) until any-motion...\n", kUseLightSleep ? "light sleep" : "busy wait");
}

void loop() {
    if (mode == Mode::Idle) {
        sleepUntilMotion();
        mode = Mode::Active;
        statusLed(0, 0, kLedLevel);
        Serial.printf("\n[%lu ms] any-motion: active, reading every %lu ms\n", static_cast<unsigned long>(millis()),
                      static_cast<unsigned long>(kActiveReadIntervalMs));
    } else {
        // Keep a steady cadence: subtract the time spent on the last read.
        const uint32_t busy = millis() - lastReadMs;
        sleepFor(busy < kActiveReadIntervalMs ? kActiveReadIntervalMs - busy : 0);
    }

    lastReadMs = millis();
    // Latched any-motion since the last read: the person is still moving.
    if (clearImuInterrupt() & BMI270_ANY_MOT_STATUS_MASK) lastMotionMs = lastReadMs;
    Serial.printf("[%lu ms] read\n", static_cast<unsigned long>(lastReadMs));
    processFifo();
    Serial.printf("  awake for %lu ms\n", static_cast<unsigned long>(millis() - lastReadMs));

    if (millis() - lastMotionMs >= kIdleAfterStillMs) {
        mode = Mode::Idle;
        statusLed(0, 0, 0);
        Serial.printf("  still for %lu s: idle until any-motion\n", static_cast<unsigned long>(kIdleAfterStillMs / 1000));
    }
}
