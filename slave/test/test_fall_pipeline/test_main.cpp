// Unit tests for the fall pipeline (heuristic gate + CNN) in prototypes/fall_pipeline.h.
// Golden windows come from SisFall test subjects; the expected values are what
// the Python training code computed (see prototypes/fall_model/).
// Run: pio test -e esp32-s3-supermini-test -f test_fall_pipeline

#include <Arduino.h>
#include <unity.h>

#include "golden.h"

namespace {
constexpr float kPeakTolG = 1e-3f;
constexpr float kTiltTolDeg = 0.05f;
constexpr float kProbTol = 1e-4f;
constexpr uint32_t kMaxInferenceUs = 50000;  // budget per candidate window

const GoldenWindow& goldenNamed(const char* name) {
    for (const GoldenWindow& g : kGolden)
        if (strcmp(g.name, name) == 0) return g;
    TEST_FAIL_MESSAGE("golden window not found");
    return kGolden[0];
}

// Rotates accel and gyro of every frame by the same rotation (yaw a, pitch b, roll c).
void rotate(const fall::ImuFrame* in, fall::ImuFrame* out, float a, float b, float c) {
    const float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b), cc = cosf(c), sc = sinf(c);
    const float r[3][3] = {{ca * cb, ca * sb * sc - sa * cc, ca * sb * cc + sa * sc},
                           {sa * cb, sa * sb * sc + ca * cc, sa * sb * cc - ca * sc},
                           {-sb, cb * sc, cb * cc}};
    for (size_t i = 0; i < fall::kWindowFrames; i++) {
        const float acc[3] = {in[i].ax, in[i].ay, in[i].az};
        const float gyr[3] = {in[i].gx, in[i].gy, in[i].gz};
        float* dstA[3] = {&out[i].ax, &out[i].ay, &out[i].az};
        float* dstG[3] = {&out[i].gx, &out[i].gy, &out[i].gz};
        for (int k = 0; k < 3; k++) {
            *dstA[k] = r[k][0] * acc[0] + r[k][1] * acc[1] + r[k][2] * acc[2];
            *dstG[k] = r[k][0] * gyr[0] + r[k][1] * gyr[1] + r[k][2] * gyr[2];
        }
    }
}

fall::ImuFrame scratch[fall::kWindowFrames];
}  // namespace

void test_heuristic_matches_python_on_golden_windows() {
    for (const GoldenWindow& g : kGolden) {
        const fall::Features f = fall::extractFeatures(g.frames, fall::kWindowFrames);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kPeakTolG, g.peakG, f.peakG, g.name);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTiltTolDeg, g.tiltDeg, f.tiltChangeDeg, g.name);
        TEST_ASSERT_EQUAL_MESSAGE(g.passesHeuristic, fall::looksLikeFall(f), g.name);
    }
}

void test_model_matches_python_on_golden_windows() {
    for (const GoldenWindow& g : kGolden) {
        const float p = fall::fallProbability(g.frames);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kProbTol, g.probability, p, g.name);
        TEST_ASSERT_EQUAL_MESSAGE(g.probability >= fall::model::kThreshold, fall::modelConfirmsFall(p), g.name);
    }
}

void test_model_confirms_falls_and_rejects_lying_down() {
    TEST_ASSERT_TRUE(fall::modelConfirmsFall(fall::fallProbability(goldenNamed("F01_SA03_R01").frames)));
    TEST_ASSERT_TRUE(fall::modelConfirmsFall(fall::fallProbability(goldenNamed("F05_SA03_R01").frames)));
    // D13: lying down quickly. Passes the heuristic, the model must reject it.
    TEST_ASSERT_FALSE(fall::modelConfirmsFall(fall::fallProbability(goldenNamed("D13_SA23_R04").frames)));
}

void test_still_window_does_not_pass_the_heuristic() {
    for (size_t i = 0; i < fall::kWindowFrames; i++) scratch[i] = {0.1f, 0.0f, 0.995f, 0.2f, -0.1f, 0.3f};
    const fall::Features f = fall::extractFeatures(scratch, fall::kWindowFrames);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, f.peakG);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, f.tiltChangeDeg);
    TEST_ASSERT_FALSE(fall::looksLikeFall(f));
}

// The belt can sit in any orientation: rotating the sensor must not change the decision.
void test_result_does_not_depend_on_sensor_orientation() {
    const GoldenWindow& g = goldenNamed("F01_SA03_R01");
    const float reference = fall::fallProbability(g.frames);
    const float angles[][3] = {{0.5f, 0.0f, 0.0f}, {0.0f, 1.2f, 0.0f}, {2.0f, -0.7f, 2.9f}, {3.1f, 1.5f, -1.0f}};
    for (const auto& a : angles) {
        rotate(g.frames, scratch, a[0], a[1], a[2]);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, reference, fall::fallProbability(scratch));
        const fall::Features f = fall::extractFeatures(scratch, fall::kWindowFrames);
        TEST_ASSERT_FLOAT_WITHIN(kTiltTolDeg, g.tiltDeg, f.tiltChangeDeg);
    }
}

void test_inference_fits_the_time_budget() {
    const uint32_t t0 = micros();
    volatile float p = fall::fallProbability(kGolden[0].frames);
    const uint32_t us = micros() - t0;
    (void)p;
    char msg[48];
    snprintf(msg, sizeof(msg), "inference took %lu us", static_cast<unsigned long>(us));
    TEST_MESSAGE(msg);
    TEST_ASSERT_LESS_THAN_UINT32(kMaxInferenceUs, us);
}

void setup() {
    delay(2000);  // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_heuristic_matches_python_on_golden_windows);
    RUN_TEST(test_model_matches_python_on_golden_windows);
    RUN_TEST(test_model_confirms_falls_and_rejects_lying_down);
    RUN_TEST(test_still_window_does_not_pass_the_heuristic);
    RUN_TEST(test_result_does_not_depend_on_sensor_orientation);
    RUN_TEST(test_inference_fits_the_time_budget);
    UNITY_END();
}

void loop() {}
