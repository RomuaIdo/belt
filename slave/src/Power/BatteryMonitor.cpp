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

uint8_t BatteryMonitor::percentFromMv(uint32_t mv) {
    struct Point {
        uint16_t mv;
        uint8_t percent;
    };
    // Typical LiPo 1S curve, from full to empty: flat in the middle, steep at the ends.
    static constexpr Point kCurve[] = {
        {4200, 100}, {4100, 90}, {3980, 75}, {3870, 60}, {3840, 50}, {3800, 40},
        {3770, 30},  {3730, 20}, {3690, 10}, {3610, 5},  {3300, 0},
    };
    constexpr size_t kPoints = sizeof(kCurve) / sizeof(kCurve[0]);

    if (mv >= kCurve[0].mv) return 100;
    if (mv <= kCurve[kPoints - 1].mv) return 0;
    for (size_t i = 1; i < kPoints; i++) {
        const Point& hi = kCurve[i - 1];
        const Point& lo = kCurve[i];
        if (mv >= lo.mv) {
            return lo.percent + (mv - lo.mv) * (hi.percent - lo.percent) / (hi.mv - lo.mv);
        }
    }
    return 0;
}

uint32_t BatteryMonitor::msUntilDue() const {
    if (isDue()) return 0;
    return intervalMs - (nowMs() - lastReadMs);
}
