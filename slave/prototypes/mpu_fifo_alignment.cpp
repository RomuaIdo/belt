// MPU-6050 FIFO alignment test.
//
// Question: when the 1024-byte FIFO overflows (FIFO_MODE = 0, oldest data is
// overwritten), do the packets stay aligned? 1024 is not a multiple of 6, 12
// or 14, so if the chip overwrites byte by byte the read pointer may land in
// the middle of a packet and every frame read afterwards is shifted.
//
// For each FIFO layout the test runs:
//   A) no overflow: reset, let it fill to ~half, snapshot and analyze (control)
//   B) overflow, once per window in kOverrunWindowsMs: reset, wait for the
//      FIFO_OFLOW interrupt on the INT pin, let it keep overwriting for that
//      window, snapshot and analyze
//
// Analysis: with the board STILL, a reference sample is read straight from the
// data registers. Each frame in the snapshot is compared to it (accel per axis,
// temperature, gyro near zero). Every byte offset 0..packet-1 is tried; if the
// frames only match at an offset other than 0, the FIFO is misaligned.
//
// Keep the board still and not perfectly level on two axes, otherwise an
// accel axis rotation (2-byte shift) could still look valid.
//
// Wiring: SDA GPIO8, SCL GPIO9, INT GPIO7 (same as circuit_test.cpp).
// Build/flash: cd slave && pio run -e proto-mpu-fifo -t upload -t monitor
// Send any character over serial to run the suite again.

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t kSdaPin = 8;
constexpr uint8_t kSclPin = 9;
constexpr uint8_t kIntPin = 7;
constexpr uint32_t kI2cHz = 400000;
constexpr uint8_t kAddr = 0x68;

constexpr uint8_t kRegSmplrtDiv = 0x19;
constexpr uint8_t kRegConfig = 0x1A;
constexpr uint8_t kRegGyroConfig = 0x1B;
constexpr uint8_t kRegAccelConfig = 0x1C;
constexpr uint8_t kRegFifoEn = 0x23;
constexpr uint8_t kRegIntPinCfg = 0x37;
constexpr uint8_t kRegIntEnable = 0x38;
constexpr uint8_t kRegIntStatus = 0x3A;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kRegUserCtrl = 0x6A;
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegFifoCountH = 0x72;
constexpr uint8_t kRegFifoRW = 0x74;
constexpr uint8_t kRegWhoAmI = 0x75;

// Same sensor config as Sensor/Mpu6050.cpp: 100 Hz, DLPF 44 Hz, +-16 g, +-2000 dps.
constexpr uint8_t kDlpf44Hz = 0x03;  // FIFO_MODE (bit 6) = 0: overwrite when full
constexpr uint8_t kSampleRateDiv100Hz = 9;
constexpr uint8_t kGyroFs2000 = 0x18;
constexpr uint8_t kAccelFs16g = 0x18;
constexpr float kAccelLsbPerG = 2048.0f;

constexpr uint8_t kFifoEnTemp = 0x80;
constexpr uint8_t kFifoEnGyro = 0x70;  // XG | YG | ZG
constexpr uint8_t kFifoEnAccel = 0x08;
constexpr uint8_t kUserCtrlFifoEn = 0x40;
constexpr uint8_t kUserCtrlFifoReset = 0x04;
constexpr uint8_t kIntPinLatchActiveHigh = 0x20;  // push-pull, latched, cleared by reading INT_STATUS
constexpr uint8_t kIntFifoOflow = 0x10;

constexpr size_t kFifoSize = 1024;
constexpr size_t kI2cChunk = 120;  // Arduino-ESP32 Wire buffer is 128 bytes
constexpr uint32_t kSamplePeriodMs = 10;

// Tolerances for a frame to count as "matches the reference" (board still).
constexpr int16_t kAccelTolLsb = 300;  // ~0.15 g
constexpr int16_t kTempTolLsb = 200;   // ~0.6 degC
constexpr int16_t kGyroTolLsb = 820;   // ~50 dps

// Extra time to keep overwriting after the overflow interrupt, one phase B run
// per window. If the offset drifted with the amount overwritten, it would
// change between windows; if it is always 1024 % packet, it is deterministic.
constexpr uint32_t kOverrunWindowsMs[] = {0, 370, 730, 1100, 2000};
constexpr size_t kWindowCount = sizeof(kOverrunWindowsMs) / sizeof(kOverrunWindowsMs[0]);
constexpr int kInconclusive = -1;
constexpr uint32_t kOverflowTimeoutMs = 5000;

struct Layout {
    const char* name;
    uint8_t fifoEn;
    bool hasTemp;
    bool hasGyro;
    size_t packetLen() const { return 6 + (hasTemp ? 2 : 0) + (hasGyro ? 6 : 0); }
};

const Layout kLayouts[] = {
    {"accel", kFifoEnAccel, false, false},                                  // 6 B, 1024 % 6 = 4
    {"accel+temp", kFifoEnAccel | kFifoEnTemp, true, false},                // 8 B, 1024 % 8 = 0 (control)
    {"accel+gyro", kFifoEnAccel | kFifoEnGyro, false, true},                // 12 B, 1024 % 12 = 4 (what the belt needs)
    {"accel+temp+gyro", kFifoEnAccel | kFifoEnTemp | kFifoEnGyro, true, true},  // 14 B, 1024 % 14 = 2
};

struct Reference {
    int16_t ax, ay, az, temp;
};

volatile bool intFired = false;
uint8_t fifoBuf[kFifoSize];

void IRAM_ATTR onMpuInt() { intFired = true; }

bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(kAddr, len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

uint8_t readReg(uint8_t reg) {
    uint8_t v = 0xFF;
    readRegs(reg, &v, 1);
    return v;
}

int16_t be16(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }

uint16_t fifoCount() {
    uint8_t b[2] = {0, 0};
    readRegs(kRegFifoCountH, b, 2);
    return static_cast<uint16_t>((b[0] << 8) | b[1]);
}

bool readFifo(uint8_t* out, size_t len) {
    size_t done = 0;
    while (done < len) {
        const size_t n = min(kI2cChunk, len - done);
        if (!readRegs(kRegFifoRW, out + done, n)) return false;
        done += n;
    }
    return true;
}

bool setupSensor() {
    const uint8_t id = readReg(kRegWhoAmI);
    if (id != 0x68) {
        Serial.printf("WHO_AM_I = 0x%02X (expected 0x68)\n", id);
        return false;
    }
    writeReg(kRegPwrMgmt1, 0x80);  // device reset
    delay(100);
    writeReg(kRegPwrMgmt1, 0x01);  // wake, PLL with gyro X
    delay(100);
    writeReg(kRegConfig, kDlpf44Hz);
    writeReg(kRegSmplrtDiv, kSampleRateDiv100Hz);
    writeReg(kRegGyroConfig, kGyroFs2000);
    writeReg(kRegAccelConfig, kAccelFs16g);
    writeReg(kRegIntPinCfg, kIntPinLatchActiveHigh);
    writeReg(kRegIntEnable, 0);
    writeReg(kRegFifoEn, 0);
    delay(50);
    return true;
}

// Averages a few direct register reads: what every FIFO frame should look like.
Reference readReference() {
    constexpr int kN = 16;
    int32_t ax = 0, ay = 0, az = 0, t = 0;
    for (int i = 0; i < kN; i++) {
        uint8_t b[8];
        readRegs(kRegAccelXoutH, b, sizeof(b));
        ax += be16(b);
        ay += be16(b + 2);
        az += be16(b + 4);
        t += be16(b + 6);
        delay(kSamplePeriodMs);
    }
    return {static_cast<int16_t>(ax / kN), static_cast<int16_t>(ay / kN),
            static_cast<int16_t>(az / kN), static_cast<int16_t>(t / kN)};
}

// Stops new writes, clears the FIFO and starts filling with the given layout.
void restartFifo(const Layout& layout) {
    writeReg(kRegFifoEn, 0);
    writeReg(kRegUserCtrl, kUserCtrlFifoReset);
    delay(1);
    readReg(kRegIntStatus);  // clears a latched interrupt
    intFired = false;
    writeReg(kRegUserCtrl, kUserCtrlFifoEn);
    writeReg(kRegFifoEn, layout.fifoEn);
}

bool frameMatches(const Layout& layout, const uint8_t* f, const Reference& ref) {
    if (abs(be16(f) - ref.ax) > kAccelTolLsb) return false;
    if (abs(be16(f + 2) - ref.ay) > kAccelTolLsb) return false;
    if (abs(be16(f + 4) - ref.az) > kAccelTolLsb) return false;
    size_t i = 6;
    if (layout.hasTemp) {
        if (abs(be16(f + i) - ref.temp) > kTempTolLsb) return false;
        i += 2;
    }
    if (layout.hasGyro) {
        for (int k = 0; k < 3; k++, i += 2) {
            if (abs(be16(f + i)) > kGyroTolLsb) return false;
        }
    }
    return true;
}

// Returns the byte offset where the frames start (0 = aligned), or
// kInconclusive when no offset fits well enough.
int analyze(const Layout& layout, const uint8_t* buf, size_t len, const Reference& ref) {
    const size_t pkt = layout.packetLen();
    size_t bestOffset = 0, bestValid = 0, validAt0 = 0, framesAt0 = len / pkt;

    for (size_t off = 0; off < pkt; off++) {
        const size_t frames = (len - off) / pkt;
        size_t valid = 0;
        for (size_t k = 0; k < frames; k++) {
            if (frameMatches(layout, buf + off + k * pkt, ref)) valid++;
        }
        if (off == 0) validAt0 = valid;
        if (valid > bestValid) {
            bestValid = valid;
            bestOffset = off;
        }
    }

    Serial.printf("    bytes=%u (%u%%%u = %u)  offset 0: %u/%u frames match  best offset: %u (%u match)\n",
                  static_cast<unsigned>(len), static_cast<unsigned>(len), static_cast<unsigned>(pkt),
                  static_cast<unsigned>(len % pkt), static_cast<unsigned>(validAt0),
                  static_cast<unsigned>(framesAt0), static_cast<unsigned>(bestOffset),
                  static_cast<unsigned>(bestValid));

    // Per-frame map at offset 0: '.' matches, 'x' does not. Shows whether a
    // misalignment covers the whole snapshot or only part of it.
    Serial.print("    map@0: ");
    for (size_t k = 0; k < framesAt0; k++) {
        Serial.print(frameMatches(layout, buf + k * pkt, ref) ? '.' : 'x');
    }
    Serial.println();

    if (bestValid < framesAt0 / 2) {
        Serial.println("    -> INCONCLUSIVE (few frames match at any offset: was the board moving?)");
        return kInconclusive;
    }
    if (bestOffset != 0 && bestValid > validAt0) {
        Serial.printf("    -> MISALIGNED: frames start %u byte(s) into the read\n", static_cast<unsigned>(bestOffset));
        return static_cast<int>(bestOffset);
    }
    Serial.println("    -> ALIGNED");
    return 0;
}

// Stops writes (so the snapshot is stable while we read) and reads everything.
size_t snapshot() {
    writeReg(kRegFifoEn, 0);
    const uint16_t count = fifoCount();
    const size_t len = min(static_cast<size_t>(count), kFifoSize);
    if (!readFifo(fifoBuf, len)) {
        Serial.println("    FIFO read failed");
        return 0;
    }
    return len;
}

int runPhaseNoOverflow(const Layout& layout, const Reference& ref) {
    Serial.println("  [A] no overflow");
    restartFifo(layout);
    const uint32_t start = millis();
    while (fifoCount() < kFifoSize / 2 && millis() - start < kOverflowTimeoutMs) delay(kSamplePeriodMs);
    const size_t len = snapshot();
    return len ? analyze(layout, fifoBuf, len, ref) : kInconclusive;
}

int runPhaseOverflow(const Layout& layout, const Reference& ref, uint32_t overrunMs) {
    Serial.printf("  [B] overflow + %lu ms overwriting (FIFO_OFLOW interrupt on INT pin)\n",
                  static_cast<unsigned long>(overrunMs));
    writeReg(kRegIntEnable, kIntFifoOflow);
    restartFifo(layout);
    const uint32_t start = millis();
    while (!intFired && millis() - start < kOverflowTimeoutMs) delay(1);
    const uint32_t elapsed = millis() - start;
    const uint8_t status = readReg(kRegIntStatus);  // also clears the latch
    writeReg(kRegIntEnable, 0);

    const uint32_t expectedMs = kFifoSize * kSamplePeriodMs / layout.packetLen();
    if (!intFired) {
        Serial.printf("    no interrupt after %lu ms (INT_STATUS=0x%02X, pin=%d)\n",
                      static_cast<unsigned long>(elapsed), status, digitalRead(kIntPin));
    } else {
        Serial.printf("    interrupt after %lu ms (expected ~%lu)  INT_STATUS=0x%02X%s\n",
                      static_cast<unsigned long>(elapsed), static_cast<unsigned long>(expectedMs), status,
                      (status & kIntFifoOflow) ? " FIFO_OFLOW" : "");
    }

    delay(overrunMs);
    const size_t len = snapshot();
    return len ? analyze(layout, fifoBuf, len, ref) : kInconclusive;
}

void printOffset(int offset) {
    if (offset == kInconclusive) Serial.printf(" %6s", "?");
    else Serial.printf(" %6d", offset);
}

void runSuite() {
    Serial.println("\n===== MPU-6050 FIFO alignment test (keep the board still) =====");
    const Reference ref = readReference();
    Serial.printf("reference: ax=%d ay=%d az=%d (%.2f %.2f %.2f g) temp=%d (%.1f C)\n", ref.ax, ref.ay,
                  ref.az, ref.ax / kAccelLsbPerG, ref.ay / kAccelLsbPerG, ref.az / kAccelLsbPerG, ref.temp,
                  ref.temp / 340.0f + 36.53f);

    constexpr size_t kLayoutCount = sizeof(kLayouts) / sizeof(kLayouts[0]);
    int noOverflow[kLayoutCount];
    int overflow[kLayoutCount][kWindowCount];

    for (size_t l = 0; l < kLayoutCount; l++) {
        const Layout& layout = kLayouts[l];
        Serial.printf("\n-- layout %s (%u bytes/packet, 1024 %% %u = %u)\n", layout.name,
                      static_cast<unsigned>(layout.packetLen()), static_cast<unsigned>(layout.packetLen()),
                      static_cast<unsigned>(kFifoSize % layout.packetLen()));
        noOverflow[l] = runPhaseNoOverflow(layout, ref);
        for (size_t w = 0; w < kWindowCount; w++) {
            overflow[l][w] = runPhaseOverflow(layout, ref, kOverrunWindowsMs[w]);
        }
    }
    writeReg(kRegFifoEn, 0);

    // Offset where the frames start (0 = aligned, ? = inconclusive). The
    // hypothesis "the full FIFO starts with the tail of a dropped packet" holds
    // when every overflow column equals the 1024%pkt column.
    Serial.println("\n===== summary: frame offset in bytes =====");
    Serial.printf("%-16s %4s %8s %6s", "layout", "pkt", "1024%pkt", "no-ovf");
    for (size_t w = 0; w < kWindowCount; w++) {
        Serial.printf(" %4lums", static_cast<unsigned long>(kOverrunWindowsMs[w]));
    }
    Serial.println("  hypothesis");
    for (size_t l = 0; l < kLayoutCount; l++) {
        const size_t pkt = kLayouts[l].packetLen();
        const int expected = static_cast<int>(kFifoSize % pkt);
        bool holds = noOverflow[l] == 0;
        Serial.printf("%-16s %4u %8d", kLayouts[l].name, static_cast<unsigned>(pkt), expected);
        printOffset(noOverflow[l]);
        for (size_t w = 0; w < kWindowCount; w++) {
            printOffset(overflow[l][w]);
            holds = holds && overflow[l][w] == expected;
        }
        Serial.println(holds ? "  holds" : "  FAILS");
    }
    Serial.println("\n===== done. Send any character to run again =====");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(3000);  // USB CDC needs time to enumerate

    Wire.begin(kSdaPin, kSclPin, kI2cHz);
    pinMode(kIntPin, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(kIntPin), onMpuInt, RISING);

    while (!setupSensor()) delay(2000);
    runSuite();
}

void loop() {
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        runSuite();
    }
    delay(50);
}
