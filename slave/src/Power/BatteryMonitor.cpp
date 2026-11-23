#include "Power/BatteryMonitor.h"

BatteryMonitor::BatteryMonitor(ReadMvFn readPinMv, uint8_t dividerRatio, uint32_t lowMv, uint32_t recoverMv,
                               uint32_t intervalMs, NowMsFn nowMs)
    : readPinMv(std::move(readPinMv)),
      dividerRatio(dividerRatio),
      lowMv(lowMv),
      recoverMv(recoverMv),
      intervalMs(intervalMs),
      nowMs(std::move(nowMs)) {}

bool BatteryMonitor::measure() {
    batteryMv = readPinMv() * dividerRatio;
    lastReadMs = nowMs();
    measured = true;

    if (low) {
        if (batteryMv > recoverMv) low = false;
    } else if (batteryMv < lowMv) {
        low = true;
    }
    return low;
}

bool BatteryMonitor::isDue() const {
    return !measured || nowMs() - lastReadMs >= intervalMs;
}

uint32_t BatteryMonitor::msUntilDue() const {
    if (isDue()) return 0;
    return intervalMs - (nowMs() - lastReadMs);
}
