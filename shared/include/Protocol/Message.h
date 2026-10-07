#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Wire protocol shared between master and slave. Every frame starts with a Header;
// a frame whose magic, type or size does not match is discarded.
//
// Pairing is always started by the belt. It scans channels 1..13 because only the
// master knows the router's channel:
//   belt   -> broadcast: PairRequest, once per channel
//   master -> belt:      PairWait (new belt, waiting for the caregiver to press Save)
//                        or PairAccept (belt already registered)
//   belt locks the channel it heard the master on and repeats PairRequest (unicast)
//   master -> belt:      PairAccept (caregiver pressed Save), carrying the master's channel
//   belt saves master MAC + channel and answers PairConfirm, echoing the PairAccept seq
//   master saves the belt only after that PairConfirm
// The belt must answer every PairAccept: the master resends it until it hears back.
//
// Alerts: belt -> master Alert; master -> belt AlertAck, echoing the Alert seq.
// The belt resends an Alert with the same seq until it is acknowledged.
namespace Protocol {

// Frame marker. It only filters out ESP-NOW traffic from other projects; it is
// not a secret. 0x42454C54 is "BELT" read as a number; on the wire (little-endian)
// the bytes are 54 4C 45 42.
constexpr uint32_t kMagic = 0x42454C54;

constexpr const char* kBroadcastMac = "FF:FF:FF:FF:FF:FF";

enum class MessageType : uint8_t {
    PairRequest = 0x01,
    PairWait = 0x02,
    PairAccept = 0x03,
    PairConfirm = 0x04,
    Alert = 0x10,
    AlertAck = 0x11,
};

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    MessageType type;
    uint16_t seq;  // sender's counter; replies echo the seq of the message they answer
};
#pragma pack(pop)

constexpr size_t kHeaderSize = sizeof(Header);
// PairAccept = Header + the master's Wi-Fi channel.
constexpr size_t kPairAcceptChannelOffset = kHeaderSize;
constexpr size_t kMaxFrameSize = kHeaderSize + 1;  // largest frame defined

// Exact size of each frame type; 0 for an unknown type.
inline size_t expectedSize(MessageType type) {
    switch (type) {
        case MessageType::PairRequest:
        case MessageType::PairWait:
        case MessageType::PairConfirm:
        case MessageType::Alert:
        case MessageType::AlertAck:
            return kHeaderSize;
        case MessageType::PairAccept:
            return kHeaderSize + 1;
    }
    return 0;
}

// Checks magic, known type and exact size. Fills `out` only when valid.
inline bool parseFrame(const uint8_t* data, int len, Header& out) {
    if (data == nullptr || len < static_cast<int>(kHeaderSize)) return false;

    Header header;
    memcpy(&header, data, kHeaderSize);  // memcpy: `data` may be misaligned
    if (header.magic != kMagic) return false;

    const size_t expected = expectedSize(header.type);
    if (expected == 0 || static_cast<size_t>(len) != expected) return false;

    out = header;
    return true;
}

// Writes a header-only frame. Returns its size, or 0 if `capacity` is too small.
inline size_t buildFrame(uint8_t* buffer, size_t capacity, MessageType type, uint16_t seq) {
    if (capacity < kHeaderSize) return 0;
    const Header header = {kMagic, type, seq};
    memcpy(buffer, &header, kHeaderSize);
    return kHeaderSize;
}

inline size_t buildPairAccept(uint8_t* buffer, size_t capacity, uint16_t seq, uint8_t channel) {
    if (capacity < kHeaderSize + 1) return 0;
    buildFrame(buffer, capacity, MessageType::PairAccept, seq);
    buffer[kPairAcceptChannelOffset] = channel;
    return kHeaderSize + 1;
}

}  // namespace Protocol
