// Unit tests for BatteryMonitor (divider reading, hysteresis, periodic interval).
// Run: pio test -e esp32-s3-supermini-test -f test_battery_monitor

#include <Arduino.h>
#include <unity.h>

#include "Power/BatteryMonitor.h"

namespace {
constexpr uint8_t kRatio = 2;
constexpr uint32_t kLowMv = 3500;
constexpr uint32_t kRecoverMv = 3650;
constexpr uint32_t kIntervalMs = 30UL * 60UL * 1000UL;

uint32_t fakeNow = 0;
uint32_t fakePinMv = 0;

BatteryMonitor makeMonitor() {
    fakeNow = 0;
    fakePinMv = 2000;  // 4000 mV battery
    return BatteryMonitor([]() { return fakePinMv; }, kRatio, kLowMv, kRecoverMv, kIntervalMs,
                          []() { return fakeNow; });
}

void setBatteryMv(uint32_t mv) { fakePinMv = mv / kRatio; }
}  // namespace

void test_due_before_the_first_reading() {
    BatteryMonitor monitor = makeMonitor();
    TEST_ASSERT_TRUE(monitor.isDue());
    TEST_ASSERT_EQUAL_UINT32(0, monitor.msUntilDue());
    TEST_ASSERT_FALSE(monitor.isLow());
}

void test_applies_the_divider_ratio() {
    BatteryMonitor monitor = makeMonitor();
    fakePinMv = 1900;
    TEST_ASSERT_FALSE(monitor.measure());
    TEST_ASSERT_EQUAL_UINT32(3800, monitor.lastBatteryMv());
}

void test_interval_restarts_on_every_reading() {
    BatteryMonitor monitor = makeMonitor();
    fakeNow = 1000;
    monitor.measure();
    TEST_ASSERT_FALSE(monitor.isDue());
    TEST_ASSERT_EQUAL_UINT32(kIntervalMs, monitor.msUntilDue());

    fakeNow = 1000 + 600000;  // a button click 10 min later
    TEST_ASSERT_EQUAL_UINT32(kIntervalMs - 600000, monitor.msUntilDue());
    monitor.measure();
    TEST_ASSERT_EQUAL_UINT32(kIntervalMs, monitor.msUntilDue());

    fakeNow += kIntervalMs - 1;
    TEST_ASSERT_FALSE(monitor.isDue());
    fakeNow += 1;
    TEST_ASSERT_TRUE(monitor.isDue());
    TEST_ASSERT_EQUAL_UINT32(0, monitor.msUntilDue());
}

void test_low_below_threshold_and_stays_low_on_every_reading() {
    BatteryMonitor monitor = makeMonitor();
    setBatteryMv(3500);
    TEST_ASSERT_FALSE(monitor.measure());  // exactly the threshold is not low
    setBatteryMv(3480);
    TEST_ASSERT_TRUE(monitor.measure());
    TEST_ASSERT_TRUE(monitor.isLow());
    TEST_ASSERT_TRUE(monitor.measure());  // every reading while low reports it
}

void test_hysteresis_before_recovering() {
    BatteryMonitor monitor = makeMonitor();
    setBatteryMv(3400);
    monitor.measure();

    setBatteryMv(3600);  // between low and recover: still low
    TEST_ASSERT_TRUE(monitor.measure());
    setBatteryMv(3650);  // exactly the recover threshold: still low
    TEST_ASSERT_TRUE(monitor.measure());
    setBatteryMv(3700);
    TEST_ASSERT_FALSE(monitor.measure());
    TEST_ASSERT_FALSE(monitor.isLow());

    setBatteryMv(3600);  // above the low threshold: not low again
    TEST_ASSERT_FALSE(monitor.measure());
}

void test_percent_follows_the_lipo_curve() {
    TEST_ASSERT_EQUAL_UINT8(100, BatteryMonitor::percentFromMv(4300));  // above full
    TEST_ASSERT_EQUAL_UINT8(100, BatteryMonitor::percentFromMv(4200));
    TEST_ASSERT_EQUAL_UINT8(95, BatteryMonitor::percentFromMv(4150));   // between 4100 (90) and 4200
    TEST_ASSERT_EQUAL_UINT8(50, BatteryMonitor::percentFromMv(3840));
    TEST_ASSERT_EQUAL_UINT8(5, BatteryMonitor::percentFromMv(3610));
    TEST_ASSERT_EQUAL_UINT8(0, BatteryMonitor::percentFromMv(3300));
    TEST_ASSERT_EQUAL_UINT8(0, BatteryMonitor::percentFromMv(3000));  // below empty

    uint8_t previous = 0;  // never goes down as the voltage rises
    for (uint32_t mv = 3000; mv <= 4300; mv += 10) {
        const uint8_t percent = BatteryMonitor::percentFromMv(mv);
        TEST_ASSERT_TRUE(percent >= previous);
        previous = percent;
    }
}

void test_last_percent_uses_the_last_reading() {
    BatteryMonitor monitor = makeMonitor();
    setBatteryMv(3840);
    monitor.measure();
    TEST_ASSERT_EQUAL_UINT8(50, monitor.lastPercent());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_due_before_the_first_reading);
    RUN_TEST(test_applies_the_divider_ratio);
    RUN_TEST(test_interval_restarts_on_every_reading);
    RUN_TEST(test_low_below_threshold_and_stays_low_on_every_reading);
    RUN_TEST(test_hysteresis_before_recovering);
    RUN_TEST(test_percent_follows_the_lipo_curve);
    RUN_TEST(test_last_percent_uses_the_last_reading);
    UNITY_END();
}

void loop() {}
