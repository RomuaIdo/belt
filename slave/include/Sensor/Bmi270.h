#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "Fall/FallPipeline.h"
#include "SparkFun_BMI270_Arduino_Library.h"

// BMI270 over SPI, set up for fall detection:
//   - accel +-16 g and gyro +-2000 dps (an impact does not saturate), both at 50 Hz;
//   - FIFO with accel + gyro, headerless, stream mode (oldest frames dropped when
//     full): 2 KB = ~170 frames = ~3.4 s of history;
//   - any-motion (~0.25 g) latched on INT1: the pin stays high until takeMotion().
//
// The caller owns the bus: call SPI.begin(sck, miso, mosi, cs) before begin().
// The BMI270 starts in I2C mode and switches to SPI on the first CS rising edge.
class Bmi270 {
public:
    static constexpr size_t kFifoBytes = 2048;
    static constexpr size_t kFrameBytes = 12;  // headerless accel + gyro
    static constexpr size_t kFifoMaxFrames = kFifoBytes / kFrameBytes;
    // A full FIFO read back 168 frames on the board: it counts as full two frames
    // before the 2048 B end (datasheet 4.7.5) and then drops the oldest frames.
    static constexpr size_t kFifoFullFrames = kFifoMaxFrames - 4;

    Bmi270(uint8_t csPin, uint32_t spiHz, SPIClass& spi = SPI);

    bool begin();

    // Reads the whole FIFO into `out` (room for kFifoMaxFrames), oldest first, and
    // returns the frame count. `lostFrames` is true when frames between the previous
    // read and these were dropped (FIFO was full, or a misaligned read was flushed),
    // so they are not contiguous with what was read before.
    size_t readFifo(fall::ImuFrame* out, bool& lostFrames);

    // Any-motion seen since the last call. Also clears the INT1 latch.
    bool takeMotion();

private:
    bool check(const char* what, int8_t err);
    void spiRead(uint8_t reg, uint8_t* buf, size_t len);

    BMI270 imu;
    uint8_t csPin;
    uint32_t spiHz;
    SPIClass& spi;
    uint8_t raw[kFifoBytes];
};
