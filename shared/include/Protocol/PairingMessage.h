#pragma once

#include <cstdint>
#include <cstddef>

// Wire-protocol definition shared between master/ and slave/ (via each
// project's "-I../shared/include" build flag) so the format can't drift.
namespace Protocol {

// Never collides with EspNowTransceiver::sendAck's raw {'A', status} payload ('A' == 0x41).
constexpr uint8_t kMagicByte = 0xC1;

constexpr const char* kBroadcastMac = "FF:FF:FF:FF:FF:FF";

enum class MessageType : uint8_t {
    PairRequest = 0x01,  // master -> broadcast, repeated while its pairing window is open
    PairResponse = 0x02, // slave -> master, unicast, sent once per accepted PairRequest
    // 0x10/0x11 reserved for a future tagged FallAlert/FallAck protocol.
};

#pragma pack(push, 1)
struct PairingMessage {
    uint8_t magic = kMagicByte;
    MessageType type;
};
#pragma pack(pop)

constexpr size_t kPairingMessageSize = sizeof(PairingMessage); // 2 bytes

// True only for a correctly-framed PairingMessage (right length + magic byte).
inline bool isPairingMessage(const uint8_t* data, int len) {
    return data != nullptr && len == static_cast<int>(kPairingMessageSize) && data[0] == kMagicByte;
}

} // namespace Protocol
