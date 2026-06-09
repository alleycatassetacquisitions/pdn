#pragma once

#include <cstddef>
#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t
{
    kPlayerInfoBroadcast = 0,
    kQuickdrawCommand = 1,
    kDebugPacket = 2,
    kChainGameEvent = 3,
    kChainConfirm = 4,
    kShootoutCommand = 5,
    kSymbolMatchCommand = 6,
    kAck = 7,
    kNumPacketTypes //Not a real packet type, DO NOT USE
};

// Max length of a player name on the wire (no null terminator).
inline constexpr size_t kNameLength = 12;

// Identifies a reliable channel by (PktType, subType). The Resender keys its
// per-channel stats with this and WirelessTransport keys its channel registry
// with it; they must agree, so both the type and the encoding live here in one
// place.
using ChannelKey = uint32_t;
inline constexpr ChannelKey channelKey(PktType type, uint8_t subType) {
    return (static_cast<ChannelKey>(subType) << 16) |
           static_cast<ChannelKey>(static_cast<uint8_t>(type));
}

struct AckPayload
{
    uint8_t originalType;
    uint8_t subType;
    uint8_t seqId;
} __attribute__((packed));

struct DataPktHdr
{
    //Total packet length including header
    uint8_t pktLen;
    PktType packetType;
    uint8_t numPktsInCluster;
    uint8_t idxInCluster;
} __attribute__((packed));

struct ChainConfirmPayload
{
    uint8_t originatorMac[6];
    uint8_t seqId;
} __attribute__((packed));

enum class ShootoutCmd : uint8_t
{
    CONFIRM = 0,
    MATCH_START = 1,
    MATCH_RESULT = 2,
    TOURNAMENT_END = 3,
    PEER_LOST = 4,
    ABORT = 5,
    BRACKET_ENTRY = 6,
};

struct ChainGameEventPayload
{
    uint8_t event_type; // ChainGameEventType
    uint8_t seqId;      // 0 = no-ack/no-retry; nonzero = reliable
} __attribute__((packed));

struct ShootoutConfirmPayload {
    uint8_t cmd;
    uint8_t seqId;
    uint8_t mac[6];
    char    name[kNameLength];
} __attribute__((packed));

struct ShootoutMatchStartPayload {
    uint8_t cmd;
    uint8_t seqId;
    uint8_t duelistA[6];
    uint8_t duelistB[6];
    uint8_t matchIndex;
} __attribute__((packed));

struct ShootoutMatchResultPayload {
    uint8_t cmd;
    uint8_t seqId;
    uint8_t winner[6];
    uint8_t loser[6];
    uint8_t matchIndex;
} __attribute__((packed));

struct ShootoutBracketEntryPayload {
    uint8_t cmd;          // ShootoutCmd::BRACKET_ENTRY
    uint8_t seqId;
    uint8_t batchId;
    uint8_t slot;
    uint8_t totalSlots;
    uint8_t mac[6];
} __attribute__((packed));

struct ShootoutTournamentEndPayload {
    uint8_t cmd;
    uint8_t seqId;
    uint8_t winner[6];
} __attribute__((packed));

struct ShootoutPeerLostPayload {
    uint8_t cmd;
    uint8_t seqId;
    uint8_t mac[6];
} __attribute__((packed));

struct ShootoutAbortPayload {
    uint8_t cmd;
    uint8_t seqId;
} __attribute__((packed));

