#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

// Wire-protocol definition shared between master and slave.
// Symmetric protocol: receiving a valid PairRequest saves peer MAC and replies
// with PairResponse; receiving PairResponse saves peer MAC.
namespace Protocol {

// Magic byte distinguishing pairing messages from alert/ACK packets.
constexpr uint8_t kMagicByte = 0xC1;

constexpr const char* kBroadcastMac = "FF:FF:FF:FF:FF:FF";

// Shared secret for validating pairing requests.
constexpr size_t kPairingKeyLength = 16;
constexpr char kPairingKey[kPairingKeyLength] = "CintoAlertaPair";

enum class MessageType : uint8_t {
    PairRequest = 0x01,
    PairResponse = 0x02,
    // 0x10/0x11 reserved for future FallAlert/FallAck.
};

#pragma pack(push, 1)
struct PairingMessage {
    uint8_t magic = kMagicByte;
    MessageType type;
    char key[kPairingKeyLength];
};
#pragma pack(pop)

constexpr size_t kPairingMessageSize = sizeof(PairingMessage);

inline void fillKey(PairingMessage& msg) {
    memcpy(msg.key, kPairingKey, kPairingKeyLength);
}

inline bool hasValidKey(const PairingMessage& msg) {
    return memcmp(msg.key, kPairingKey, kPairingKeyLength) == 0;
}

// Validates message size, magic byte, and pairing key.
inline bool isPairingMessage(const uint8_t* data, int len) {
    if (data == nullptr || len != static_cast<int>(kPairingMessageSize) || data[0] != kMagicByte) {
        return false;
    }
    return hasValidKey(*reinterpret_cast<const PairingMessage*>(data));
}

} // namespace Protocol
