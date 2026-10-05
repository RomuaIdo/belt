// Unit tests for Protocol::PairingMessage framing: correct size/magic/key, and that it can
// never be confused with the existing (untagged) EspNowTransceiver::sendAck payload.
// Run with: pio test -e esp32-s3-devkitc-1-test -f test_protocol

#include <Arduino.h>
#include <unity.h>

#include "Protocol/PairingMessage.h"

void test_pairing_message_size_matches_magic_type_and_key() {
    TEST_ASSERT_EQUAL_UINT32(1 + 1 + Protocol::kPairingKeyLength,
                               static_cast<uint32_t>(Protocol::kPairingMessageSize));
}

void test_fill_key_round_trips_through_has_valid_key() {
    Protocol::PairingMessage msg{};
    msg.type = Protocol::MessageType::PairRequest;
    Protocol::fillKey(msg);

    TEST_ASSERT_TRUE(Protocol::hasValidKey(msg));
}

void test_is_pairing_message_true_for_well_framed_message() {
    Protocol::PairingMessage msg{};
    msg.type = Protocol::MessageType::PairRequest;
    Protocol::fillKey(msg);

    TEST_ASSERT_TRUE(Protocol::isPairingMessage(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg)));
}

void test_is_pairing_message_false_for_wrong_key() {
    Protocol::PairingMessage msg{};
    msg.type = Protocol::MessageType::PairRequest;
    memset(msg.key, 0, sizeof(msg.key)); // never fill it: garbage/zeroed key

    TEST_ASSERT_FALSE(Protocol::isPairingMessage(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg)));
}

void test_is_pairing_message_false_for_wrong_length() {
    Protocol::PairingMessage msg{};
    msg.type = Protocol::MessageType::PairResponse;
    Protocol::fillKey(msg);

    TEST_ASSERT_FALSE(Protocol::isPairingMessage(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg) + 1));
    TEST_ASSERT_FALSE(Protocol::isPairingMessage(reinterpret_cast<const uint8_t*>(&msg), 0));
}

void test_is_pairing_message_false_for_null_data() {
    TEST_ASSERT_FALSE(Protocol::isPairingMessage(nullptr, static_cast<int>(Protocol::kPairingMessageSize)));
}

void test_is_pairing_message_rejects_the_existing_sendack_payload() {
    // EspNowTransceiver::sendAck's own (untagged, legacy) payload: {'A', status}.
    const uint8_t ackPayload[2] = {'A', 0x01};
    TEST_ASSERT_FALSE(Protocol::isPairingMessage(ackPayload, sizeof(ackPayload)));
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_pairing_message_size_matches_magic_type_and_key);
    RUN_TEST(test_fill_key_round_trips_through_has_valid_key);
    RUN_TEST(test_is_pairing_message_true_for_well_framed_message);
    RUN_TEST(test_is_pairing_message_false_for_wrong_key);
    RUN_TEST(test_is_pairing_message_false_for_wrong_length);
    RUN_TEST(test_is_pairing_message_false_for_null_data);
    RUN_TEST(test_is_pairing_message_rejects_the_existing_sendack_payload);
    UNITY_END();
}

void loop() {}
