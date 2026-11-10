// Unit tests for FallDetector: FIFO reads in, fall decisions out.
// Uses the SisFall golden windows from test_fall_pipeline.
// Run: pio test -e esp32-s3-supermini-test -f test_fall_detector

#include <Arduino.h>
#include <unity.h>

#include "../test_fall_pipeline/golden.h"
#include "Fall/FallDetector.h"

namespace {
constexpr size_t kHalf = fall::kWindowFrames / 2;

FallDetector detector;  // ~6.5 KB: not on the stack
fall::ImuFrame still[fall::kWindowFrames];

const GoldenWindow& goldenNamed(const char* name) {
    for (const GoldenWindow& g : kGolden)
        if (strcmp(g.name, name) == 0) return g;
    TEST_FAIL_MESSAGE("golden window not found");
    return kGolden[0];
}

void fillStill() {
    for (size_t i = 0; i < fall::kWindowFrames; i++) still[i] = {0.0f, 0.0f, 1.0f, 0.1f, -0.1f, 0.2f};
}

// Feeds a whole window as two reads of 50 frames; returns the second result.
FallDetector::Result feedInTwoReads(const fall::ImuFrame* window) {
    const FallDetector::Result first = detector.process(window, kHalf, false);
    TEST_ASSERT_EQUAL(0, first.windowsChecked);  // not 2 s of data yet
    return detector.process(window + kHalf, kHalf, false);
}
}  // namespace

void setUp() {
    detector.reset();
    fillStill();
}

void tearDown() {}

void test_fall_split_across_two_reads_is_confirmed() {
    const FallDetector::Result r = feedInTwoReads(goldenNamed("F01_SA03_R01").frames);
    TEST_ASSERT_TRUE(r.candidate);
    TEST_ASSERT_TRUE(r.fall);
    TEST_ASSERT_TRUE(r.probability >= fall::model::kThreshold);
}

void test_lying_down_passes_the_heuristic_but_not_the_model() {
    const FallDetector::Result r = feedInTwoReads(goldenNamed("D13_SA23_R04").frames);
    TEST_ASSERT_TRUE(r.candidate);
    TEST_ASSERT_FALSE(r.fall);
}

void test_still_frames_are_not_a_candidate() {
    const FallDetector::Result r = feedInTwoReads(still);
    TEST_ASSERT_FALSE(r.candidate);
    TEST_ASSERT_FALSE(r.fall);
    TEST_ASSERT_EQUAL(1, r.windowsChecked);
}

void test_lost_frames_break_the_history() {
    const fall::ImuFrame* w = goldenNamed("F01_SA03_R01").frames;
    detector.process(w, kHalf, false);
    // Frames in between were dropped: the second half alone is not 2 s.
    const FallDetector::Result r = detector.process(w + kHalf, kHalf, true);
    TEST_ASSERT_EQUAL(0, r.windowsChecked);
    TEST_ASSERT_FALSE(r.fall);
}

void test_a_fall_is_reported_once() {
    TEST_ASSERT_TRUE(feedInTwoReads(goldenNamed("F01_SA03_R01").frames).fall);
    TEST_ASSERT_EQUAL(0, detector.historySize());

    // The next reads must not find the same impact again.
    TEST_ASSERT_FALSE(detector.process(still, kHalf, false).fall);
    TEST_ASSERT_FALSE(detector.process(still + kHalf, kHalf, false).fall);
}

void test_one_read_holding_a_whole_window_is_checked() {
    const FallDetector::Result r = detector.process(goldenNamed("F05_SA03_R01").frames, fall::kWindowFrames, false);
    TEST_ASSERT_EQUAL(1, r.windowsChecked);  // only the window ending in the newest frame fits
    TEST_ASSERT_TRUE(r.fall);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_fall_split_across_two_reads_is_confirmed);
    RUN_TEST(test_lying_down_passes_the_heuristic_but_not_the_model);
    RUN_TEST(test_still_frames_are_not_a_candidate);
    RUN_TEST(test_lost_frames_break_the_history);
    RUN_TEST(test_a_fall_is_reported_once);
    RUN_TEST(test_one_read_holding_a_whole_window_is_checked);
    UNITY_END();
}

void loop() {}
