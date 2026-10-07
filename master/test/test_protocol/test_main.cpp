// Unit tests for the shared wire protocol (shared/include/Protocol/Message.h).
// Run: pio test -e esp32-s3-devkitc-1-test -f test_protocol
// No Wi-Fi, credentials or flash needed.

#include <Arduino.h>
#include <unity.h>

#include "Protocol/Message.h"

using namespace Protocol;

namespace {
uint8_t buffer[kMaxFrameSize + 4];

size_t headerOnlyFrame(MessageType type, uint16_t seq) {
    return buildFrame(buffer, sizeof(buffer), type, seq);
}
}  // namespace

void test_header_is_seven_bytes() {
    TEST_ASSERT_EQUAL_UINT32(7, kHeaderSize);
    TEST_ASSERT_EQUAL_UINT32(8, kMaxFrameSize);
}

void test_wire_bytes_are_stable() {
    // These bytes are what a belt built from the same header must produce.
    size_t size = headerOnlyFrame(MessageType::PairRequest, 1);
    const uint8_t request[] = {0x54, 0x4C, 0x45, 0x42, 0x01, 0x01, 0x00};
    TEST_ASSERT_EQUAL_UINT32(sizeof(request), size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(request, buffer, sizeof(request));

    size = buildPairAccept(buffer, sizeof(buffer), 0x0102, 6);
    const uint8_t accept[] = {0x54, 0x4C, 0x45, 0x42, 0x03, 0x02, 0x01, 0x06};
    TEST_ASSERT_EQUAL_UINT32(sizeof(accept), size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(accept, buffer, sizeof(accept));
}

void test_header_only_frames_round_trip() {
    const MessageType types[] = {MessageType::PairRequest, MessageType::PairWait,
                                 MessageType::PairConfirm, MessageType::Alert,
                                 MessageType::AlertAck};
    for (MessageType type : types) {
        const size_t size = headerOnlyFrame(type, 0xBEEF);
        TEST_ASSERT_EQUAL_UINT32(kHeaderSize, size);

        Header header;
        TEST_ASSERT_TRUE(parseFrame(buffer, static_cast<int>(size), header));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type), static_cast<uint8_t>(header.type));
        TEST_ASSERT_EQUAL_UINT16(0xBEEF, header.seq);
    }
}

void test_pair_accept_carries_the_channel() {
    const size_t size = buildPairAccept(buffer, sizeof(buffer), 7, 11);
    TEST_ASSERT_EQUAL_UINT32(kHeaderSize + 1, size);

    Header header;
    TEST_ASSERT_TRUE(parseFrame(buffer, static_cast<int>(size), header));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::PairAccept), static_cast<uint8_t>(header.type));
    TEST_ASSERT_EQUAL_UINT16(7, header.seq);
    TEST_ASSERT_EQUAL_UINT8(11, buffer[kPairAcceptChannelOffset]);
}

void test_builders_refuse_a_too_small_buffer() {
    TEST_ASSERT_EQUAL_UINT32(0, buildFrame(buffer, kHeaderSize - 1, MessageType::PairRequest, 1));
    TEST_ASSERT_EQUAL_UINT32(0, buildPairAccept(buffer, kHeaderSize, 1, 6));
}

void test_parse_accepts_misaligned_data() {
    // The ESP-NOW callback may hand over the payload at any address.
    uint8_t padded[kMaxFrameSize + 1];
    const size_t size = buildFrame(padded + 1, sizeof(padded) - 1, MessageType::PairRequest, 3);
    Header header;
    TEST_ASSERT_TRUE(parseFrame(padded + 1, static_cast<int>(size), header));
    TEST_ASSERT_EQUAL_UINT16(3, header.seq);
}

void test_parse_rejects_wrong_magic() {
    const size_t size = headerOnlyFrame(MessageType::PairRequest, 1);
    buffer[0] ^= 0xFF;
    Header header;
    TEST_ASSERT_FALSE(parseFrame(buffer, static_cast<int>(size), header));
}

void test_parse_rejects_unknown_type() {
    const size_t size = headerOnlyFrame(MessageType::PairRequest, 1);
    Header header;
    buffer[4] = 0;  // the type byte follows the 4-byte magic
    TEST_ASSERT_FALSE(parseFrame(buffer, static_cast<int>(size), header));
    buffer[4] = 99;
    TEST_ASSERT_FALSE(parseFrame(buffer, static_cast<int>(size), header));
}

void test_parse_rejects_wrong_length() {
    Header header;

    const size_t size = headerOnlyFrame(MessageType::PairRequest, 1);
    TEST_ASSERT_FALSE(parseFrame(buffer, static_cast<int>(size) - 1, header));
    TEST_ASSERT_FALSE(parseFrame(buffer, 0, header));
    TEST_ASSERT_FALSE(parseFrame(buffer, -1, header));
    TEST_ASSERT_FALSE(parseFrame(nullptr, static_cast<int>(size), header));

    // More bytes than the type holds.
    TEST_ASSERT_FALSE(parseFrame(buffer, static_cast<int>(size) + 1, header));

    // PairAccept without its channel byte.
    const size_t accept = buildPairAccept(buffer, sizeof(buffer), 1, 6);
    TEST_ASSERT_FALSE(parseFrame(buffer, static_cast<int>(accept) - 1, header));
}

void test_parse_leaves_output_untouched_when_invalid() {
    Header header = {0, MessageType::PairRequest, 0};
    const size_t size = headerOnlyFrame(MessageType::PairWait, 5);
    buffer[0] ^= 0xFF;
    TEST_ASSERT_FALSE(parseFrame(buffer, static_cast<int>(size), header));
    TEST_ASSERT_EQUAL_UINT32(0, header.magic);
    TEST_ASSERT_EQUAL_UINT16(0, header.seq);
}

void test_expected_size_per_type() {
    TEST_ASSERT_EQUAL_UINT32(kHeaderSize, expectedSize(MessageType::PairRequest));
    TEST_ASSERT_EQUAL_UINT32(kHeaderSize + 1, expectedSize(MessageType::PairAccept));
    TEST_ASSERT_EQUAL_UINT32(0, expectedSize(static_cast<MessageType>(0)));
    TEST_ASSERT_EQUAL_UINT32(0, expectedSize(static_cast<MessageType>(200)));
}

void setup() {
    delay(2000);  // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_header_is_seven_bytes);
    RUN_TEST(test_wire_bytes_are_stable);
    RUN_TEST(test_header_only_frames_round_trip);
    RUN_TEST(test_pair_accept_carries_the_channel);
    RUN_TEST(test_builders_refuse_a_too_small_buffer);
    RUN_TEST(test_parse_accepts_misaligned_data);
    RUN_TEST(test_parse_rejects_wrong_magic);
    RUN_TEST(test_parse_rejects_unknown_type);
    RUN_TEST(test_parse_rejects_wrong_length);
    RUN_TEST(test_parse_leaves_output_untouched_when_invalid);
    RUN_TEST(test_expected_size_per_type);
    UNITY_END();
}

void loop() {}
