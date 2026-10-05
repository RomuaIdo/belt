#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

// Wire-protocol definition shared between master/ and slave/ (via each
// project's "-I../shared/include" build flag) so the format can't drift.
//
// Symmetric by design: whichever side RECEIVES a valid-keyed PairRequest
// saves the sender's MAC and replies with PairResponse; whichever side
// receives that PairResponse saves the sender's MAC too. This covers both
// pairing modes with no per-mode message types:
//   - slave-initiated: slave broadcasts PairRequest across channels,
//     master (always listening) replies.
//   - master-initiated: master unicasts PairRequest to a MAC entered on
//     the dashboard, slave replies.
namespace Protocol {

// Never collides with EspNowTransceiver::sendAck's raw {'A', status} payload ('A' == 0x41).
constexpr uint8_t kMagicByte = 0xC1;

constexpr const char* kBroadcastMac = "FF:FF:FF:FF:FF:FF";

// Shared secret gating PairRequest acceptance, since a receiver may now act
// on one without first opening any explicit "pairing window".
constexpr size_t kPairingKeyLength = 16;
constexpr char kPairingKey[kPairingKeyLength] = "CintoAlertaPair";

enum class MessageType : uint8_t {
    PairRequest = 0x01,
    PairResponse = 0x02,
    // 0x10/0x11 reserved for a future tagged FallAlert/FallAck protocol.
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

// True only for a correctly-framed, correctly-keyed PairingMessage.
inline bool isPairingMessage(const uint8_t* data, int len) {
    if (data == nullptr || len != static_cast<int>(kPairingMessageSize) || data[0] != kMagicByte) {
        return false;
    }
    return hasValidKey(*reinterpret_cast<const PairingMessage*>(data));
}

} // namespace Protocol
