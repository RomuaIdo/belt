#include "Sensor/Bmi270.h"

namespace {
// any-motion: duration in 20 ms steps, threshold in 0.49 mg steps (max 2047 ~ 1 g).
// The threshold tops out at ~1 g and the BMI270 has no high-g feature, so
// any-motion also fires on ordinary movement: FallDetector does the filtering.
constexpr uint16_t kAnyMotionDuration = 1;     // 20 ms
constexpr uint16_t kAnyMotionThreshold = 512;  // ~0.25 g

// Raw registers, read directly (see readFifo()).
constexpr uint8_t kRegFifoLength0 = 0x24;
constexpr uint8_t kRegFifoData = 0x26;
constexpr int16_t kFifoOverRead = -32768;  // 0x8000: no data at this position
constexpr float kAccelLsbPerG = 2048.0f;   // +-16 g
constexpr float kGyroLsbPerDps = 16.384f;  // +-2000 dps

int16_t le16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }
}  // namespace

Bmi270::Bmi270(uint8_t csPin, uint32_t spiHz, SPIClass& spi) : csPin(csPin), spiHz(spiHz), spi(spi) {}

bool Bmi270::check(const char* what, int8_t err) {
    if (err != BMI2_OK) Serial.printf("Bmi270: %s failed (%d)\n", what, err);
    return err == BMI2_OK;
}

bool Bmi270::begin() {
    if (!check("beginSPI", imu.beginSPI(csPin, spiHz, spi))) return false;

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

    // Keeps register access simple; revisit for power once sleep is added.
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

    // Latched: the loop polls INT1, a short pulse could be missed. Reading the
    // interrupt status (takeMotion()) clears the latch.
    bmi2_int_pin_config pin;
    pin.pin_type = BMI2_INT1;
    pin.int_latch = BMI2_INT_LATCH;
    pin.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    pin.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    pin.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    pin.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    if (!check("setInterruptPinConfig", imu.setInterruptPinConfig(pin))) return false;

    if (!check("flushFIFO", imu.flushFIFO())) return false;
    takeMotion();
    return true;
}

bool Bmi270::takeMotion() {
    uint16_t status = 0;
    imu.getInterruptStatus(&status);
    return (status & BMI270_ANY_MOT_STATUS_MASK) != 0;
}

void Bmi270::spiRead(uint8_t reg, uint8_t* buf, size_t len) {
    memset(buf, 0, len);
    spi.beginTransaction(SPISettings(spiHz, MSBFIRST, SPI_MODE0));
    digitalWrite(csPin, LOW);
    spi.transfer(reg | 0x80);
    spi.transfer(0);  // BMI270 SPI reads start with one dummy byte
    spi.transfer(buf, len);
    digitalWrite(csPin, HIGH);
    spi.endTransaction();
}

// Not imu.getFIFOData(): over SPI it mallocs the FIFO length but reads one extra
// (dummy) byte into it, corrupting the heap (SparkFun lib 1.0.3).
size_t Bmi270::readFifo(fall::ImuFrame* out, bool& lostFrames) {
    uint8_t lenBuf[2];
    spiRead(kRegFifoLength0, lenBuf, sizeof(lenBuf));
    const size_t len = min(static_cast<size_t>((lenBuf[0] | (lenBuf[1] << 8)) & 0x3FFF), kFifoBytes);
    spiRead(kRegFifoData, raw, len);

    // Should not happen on the BMI270 (it drops whole frames on overflow), but a
    // misaligned read would turn every frame into garbage: drop it and start over
    // instead of feeding the model.
    if (len % kFrameBytes != 0) {
        Serial.printf("Bmi270: FIFO length %u is not a multiple of %u, flushing\n", static_cast<unsigned>(len),
                      static_cast<unsigned>(kFrameBytes));
        imu.flushFIFO();
        lostFrames = true;
        return 0;
    }

    // Headerless frame with accel + gyro: GYR x,y,z then ACC x,y,z, little endian.
    size_t n = 0;
    for (size_t off = 0; off + kFrameBytes <= len && n < kFifoMaxFrames; off += kFrameBytes) {
        const uint8_t* f = raw + off;
        if (le16(f + 6) == kFifoOverRead) break;  // read past the stored data
        fall::ImuFrame& s = out[n++];
        s.gx = le16(f) / kGyroLsbPerDps;
        s.gy = le16(f + 2) / kGyroLsbPerDps;
        s.gz = le16(f + 4) / kGyroLsbPerDps;
        s.ax = le16(f + 6) / kAccelLsbPerG;
        s.ay = le16(f + 8) / kAccelLsbPerG;
        s.az = le16(f + 10) / kAccelLsbPerG;
    }
    lostFrames = n >= kFifoFullFrames;
    return n;
}
