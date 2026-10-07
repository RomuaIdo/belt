// Unit tests for the IMU SampleBuffer (circular buffer).
// Run: pio test -e esp32-s3-supermini-test -f test_sample_buffer

#include <Arduino.h>
#include <unity.h>

#include "Domain/SampleBuffer.h"

namespace {
constexpr size_t kCapacity = 4;

// Identifies a sample by its timestamp.
ImuSample sampleAt(uint32_t tMs) { return ImuSample{0, 0, 0, 0, 0, 0, tMs}; }
}  // namespace

void test_new_buffer_is_empty() {
    SampleBuffer<kCapacity> buffer;
    TEST_ASSERT_EQUAL_UINT32(0, buffer.size());
    TEST_ASSERT_FALSE(buffer.isFull());
    TEST_ASSERT_EQUAL_UINT32(kCapacity, buffer.capacity());
}

void test_push_below_capacity_keeps_insertion_order() {
    SampleBuffer<kCapacity> buffer;
    buffer.push(sampleAt(10));
    buffer.push(sampleAt(20));
    buffer.push(sampleAt(30));

    TEST_ASSERT_EQUAL_UINT32(3, buffer.size());
    TEST_ASSERT_FALSE(buffer.isFull());
    TEST_ASSERT_EQUAL_UINT32(10, buffer.at(0).tMs);
    TEST_ASSERT_EQUAL_UINT32(20, buffer.at(1).tMs);
    TEST_ASSERT_EQUAL_UINT32(30, buffer.at(2).tMs);
    TEST_ASSERT_EQUAL_UINT32(30, buffer.latest().tMs);
}

void test_wrap_around_drops_the_oldest() {
    SampleBuffer<kCapacity> buffer;
    for (uint32_t t = 1; t <= 6; t++) buffer.push(sampleAt(t));

    TEST_ASSERT_EQUAL_UINT32(kCapacity, buffer.size());
    TEST_ASSERT_TRUE(buffer.isFull());
    // Samples 1 and 2 were overwritten: 3, 4, 5, 6 remain, oldest first.
    TEST_ASSERT_EQUAL_UINT32(3, buffer.at(0).tMs);
    TEST_ASSERT_EQUAL_UINT32(4, buffer.at(1).tMs);
    TEST_ASSERT_EQUAL_UINT32(5, buffer.at(2).tMs);
    TEST_ASSERT_EQUAL_UINT32(6, buffer.at(3).tMs);
    TEST_ASSERT_EQUAL_UINT32(6, buffer.latest().tMs);
}

void test_clear_empties_the_buffer() {
    SampleBuffer<kCapacity> buffer;
    for (uint32_t t = 1; t <= 6; t++) buffer.push(sampleAt(t));
    buffer.clear();

    TEST_ASSERT_EQUAL_UINT32(0, buffer.size());
    TEST_ASSERT_FALSE(buffer.isFull());

    buffer.push(sampleAt(100));
    TEST_ASSERT_EQUAL_UINT32(1, buffer.size());
    TEST_ASSERT_EQUAL_UINT32(100, buffer.at(0).tMs);
}

void setup() {
    delay(2000);  // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_new_buffer_is_empty);
    RUN_TEST(test_push_below_capacity_keeps_insertion_order);
    RUN_TEST(test_wrap_around_drops_the_oldest);
    RUN_TEST(test_clear_empties_the_buffer);
    UNITY_END();
}

void loop() {}
