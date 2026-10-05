// Unit tests for Protocol::PairingMessage framing (slave's copy of master/test/test_protocol).
// Run with: pio test -e esp32-s3-supermini-test -f test_protocol

#include <Arduino.h>
#include <unity.h>

#include "Protocol/PairingMessage.h"

void test_pairing_message_is_exactly_two_bytes() {
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(Protocol::kPairingMessageSize));
}

void test_is_pairing_message_true_for_well_framed_message() {
    Protocol::PairingMessage msg{};
    msg.type = Protocol::MessageType::PairResponse;

    TEST_ASSERT_TRUE(Protocol::isPairingMessage(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg)));
}

void test_is_pairing_message_false_for_wrong_length() {
    Protocol::PairingMessage msg{};
    msg.type = Protocol::MessageType::PairRequest;

    TEST_ASSERT_FALSE(Protocol::isPairingMessage(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg) + 1));
    TEST_ASSERT_FALSE(Protocol::isPairingMessage(reinterpret_cast<const uint8_t*>(&msg), 0));
}

void test_is_pairing_message_false_for_null_data() {
    TEST_ASSERT_FALSE(Protocol::isPairingMessage(nullptr, static_cast<int>(Protocol::kPairingMessageSize)));
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_pairing_message_is_exactly_two_bytes);
    RUN_TEST(test_is_pairing_message_true_for_well_framed_message);
    RUN_TEST(test_is_pairing_message_false_for_wrong_length);
    RUN_TEST(test_is_pairing_message_false_for_null_data);
    UNITY_END();
}

void loop() {}
