#pragma once

// Fall decision for a 2 s window (100 frames at 50 Hz): a cheap heuristic gate
// and the CNN that confirms its candidates. Plain C++ (no Arduino), shared by
// bmi270_fall_detect.cpp and test/test_fall_pipeline.
//
// The heuristic and the model were tuned/trained on SisFall through a
// simulation of the firmware flow, see fall_model/README.md.

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "fall_model_weights.h"

namespace fall {

struct ImuFrame {
    float ax, ay, az;  // g
    float gx, gy, gz;  // dps
};

constexpr size_t kSampleHz = 50;
constexpr size_t kWindowFrames = 100;                    // 2 s
constexpr size_t kOrientationFrames = kSampleHz / 2;     // 0.5 s at each end of the window

// Heuristic gate (recall first: the model removes the false candidates).
constexpr float kImpactG = 1.6f;          // |a| peak in the window
constexpr float kTiltChangeDeg = 40.0f;   // orientation at the start vs the end of the window
// Logged only (no gain on SisFall).
constexpr size_t kFreeFallLookbackFrames = kSampleHz;    // 1 s before the peak
constexpr size_t kRotationAroundFrames = kSampleHz / 2;  // 0.5 s around the peak

struct Features {
    size_t peakIdx;
    float peakG;
    float minGBeforePeak;
    float peakDpsNearImpact;
    float tiltChangeDeg;
};

inline float magnitude(float x, float y, float z) { return sqrtf(x * x + y * y + z * z); }
inline float accelG(const ImuFrame& f) { return magnitude(f.ax, f.ay, f.az); }
inline float gyroDps(const ImuFrame& f) { return magnitude(f.gx, f.gy, f.gz); }

// Mean accel over [from, from + count).
inline void meanAccel(const ImuFrame* w, size_t from, size_t count, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    for (size_t i = from; i < from + count; i++) {
        out[0] += w[i].ax;
        out[1] += w[i].ay;
        out[2] += w[i].az;
    }
    for (int k = 0; k < 3; k++) out[k] /= count;
}

inline Features extractFeatures(const ImuFrame* w, size_t n) {
    Features f{};
    for (size_t i = 0; i < n; i++) {
        const float g = accelG(w[i]);
        if (g > f.peakG) {
            f.peakG = g;
            f.peakIdx = i;
        }
    }

    const size_t ffFrom = f.peakIdx > kFreeFallLookbackFrames ? f.peakIdx - kFreeFallLookbackFrames : 0;
    f.minGBeforePeak = 99.0f;
    for (size_t i = ffFrom; i < f.peakIdx; i++) f.minGBeforePeak = fminf(f.minGBeforePeak, accelG(w[i]));
    if (f.peakIdx == 0) f.minGBeforePeak = accelG(w[0]);

    const size_t rotFrom = f.peakIdx > kRotationAroundFrames ? f.peakIdx - kRotationAroundFrames : 0;
    const size_t rotTo = f.peakIdx + kRotationAroundFrames + 1 < n ? f.peakIdx + kRotationAroundFrames + 1 : n;
    for (size_t i = rotFrom; i < rotTo; i++) f.peakDpsNearImpact = fmaxf(f.peakDpsNearImpact, gyroDps(w[i]));

    if (n >= 2 * kOrientationFrames) {
        float before[3], after[3];
        meanAccel(w, 0, kOrientationFrames, before);
        meanAccel(w, n - kOrientationFrames, kOrientationFrames, after);
        const float dot = before[0] * after[0] + before[1] * after[1] + before[2] * after[2];
        const float norms = magnitude(before[0], before[1], before[2]) * magnitude(after[0], after[1], after[2]);
        const float c = norms > 0.0f ? fmaxf(-1.0f, fminf(1.0f, dot / norms)) : 1.0f;
        f.tiltChangeDeg = acosf(c) * 180.0f / static_cast<float>(M_PI);
    }
    return f;
}

// An impact and the body ending in another orientation (standing -> lying).
inline bool looksLikeFall(const Features& f) { return f.peakG >= kImpactG && f.tiltChangeDeg >= kTiltChangeDeg; }

// ---- Model -----------------------------------------------------------------
// 1D CNN on 4 orientation-invariant channels, so it does not depend on how
// the sensor sits on the belt: |a|, a along the gravity at the start of the
// window, a across it, |gyro|.
//   conv(4->8, k5) relu pool2 -> conv(8->16, k5) relu pool2 -> conv(16->16, k5) relu
//   -> mean over time -> dense(16->1) -> sigmoid

constexpr size_t kModelChannels = 4;
constexpr size_t kT0 = kWindowFrames;           // 100
constexpr size_t kT1 = (kT0 - 4) / 2;           // 48 after conv1 + pool
constexpr size_t kT2 = (kT1 - 4) / 2;           // 22 after conv2 + pool
constexpr size_t kT3 = kT2 - 4;                 // 18 after conv3

// Model input [channel][time], scaled like the training data.
inline void modelInput(const ImuFrame* w, float out[kModelChannels][kWindowFrames]) {
    double g0[3] = {0, 0, 0};
    for (size_t i = 0; i < kOrientationFrames; i++) {
        g0[0] += w[i].ax;
        g0[1] += w[i].ay;
        g0[2] += w[i].az;
    }
    const double g0n = sqrt(g0[0] * g0[0] + g0[1] * g0[1] + g0[2] * g0[2]);
    const double u[3] = {g0[0] / g0n, g0[1] / g0n, g0[2] / g0n};
    for (size_t i = 0; i < kWindowFrames; i++) {
        const double ax = w[i].ax, ay = w[i].ay, az = w[i].az;
        const double amag = sqrt(ax * ax + ay * ay + az * az);
        const double av = ax * u[0] + ay * u[1] + az * u[2];
        const double ah2 = amag * amag - av * av;
        out[0][i] = static_cast<float>(amag * model::kAccScale);
        out[1][i] = static_cast<float>(av * model::kAccScale);
        out[2][i] = static_cast<float>(sqrt(ah2 > 0 ? ah2 : 0) * model::kAccScale);
        out[3][i] = gyroDps(w[i]) * model::kGyrScale;
    }
}

// Valid 1D convolution + ReLU. w is [cout][cin][k].
template <size_t Cin, size_t Cout, size_t K, size_t Tin>
inline void convRelu(const float (&in)[Cin][Tin], const float* w, const float* b, float (&out)[Cout][Tin - K + 1]) {
    for (size_t o = 0; o < Cout; o++) {
        for (size_t t = 0; t < Tin - K + 1; t++) {
            float acc = b[o];
            for (size_t c = 0; c < Cin; c++) {
                const float* wk = w + (o * Cin + c) * K;
                for (size_t j = 0; j < K; j++) acc += wk[j] * in[c][t + j];
            }
            out[o][t] = acc > 0.0f ? acc : 0.0f;
        }
    }
}

template <size_t C, size_t T>
inline void maxPool2(const float (&in)[C][T], float (&out)[C][T / 2]) {
    for (size_t c = 0; c < C; c++)
        for (size_t t = 0; t < T / 2; t++) out[c][t] = fmaxf(in[c][2 * t], in[c][2 * t + 1]);
}

// Probability that the window is a fall. ~67k multiply-adds.
// The activations take ~11.6 KB: static, not on the stack (the Arduino loop
// task has 8 KB). Not reentrant: call it from one task only.
inline float fallProbability(const ImuFrame* w) {
    static float x[kModelChannels][kT0];
    static float c1[8][kT0 - 4], p1[8][kT1];
    static float c2[16][kT1 - 4], p2[16][kT2];
    static float c3[16][kT3];
    modelInput(w, x);
    convRelu<kModelChannels, 8, 5, kT0>(x, model::kConv1W, model::kConv1B, c1);
    maxPool2(c1, p1);
    convRelu<8, 16, 5, kT1>(p1, model::kConv2W, model::kConv2B, c2);
    maxPool2(c2, p2);
    convRelu<16, 16, 5, kT2>(p2, model::kConv3W, model::kConv3B, c3);
    float z = model::kDenseB[0];
    for (size_t c = 0; c < 16; c++) {
        float mean = 0.0f;
        for (size_t t = 0; t < kT3; t++) mean += c3[c][t];
        z += model::kDenseW[c] * (mean / kT3);
    }
    return 1.0f / (1.0f + expf(-z));
}

inline bool modelConfirmsFall(float probability) { return probability >= model::kThreshold; }

}  // namespace fall
