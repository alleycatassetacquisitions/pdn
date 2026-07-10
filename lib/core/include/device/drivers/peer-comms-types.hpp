#pragma once

#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t {
    kPlayerInfoBroadcast = 0,
    kQuickdrawCommand = 1,
    kDebugPacket = 2,
    kHandshakeCommand = 3,
    kChainAnnouncement = 4,
    kChainAnnouncementAck = 5,
    kChainGameEvent = 6,
    kChainConfirm = 7,
    kRoleAnnounce = 8,
    kRoleAnnounceAck = 9,
    kChainGameEventAck = 10,
    kShootoutCommand = 11,
    kShootoutCommandAck = 12,
    kSymbolMatchCommand = 13,
    kFdnConnect = 14,
    kPdnConnectionContext = 15,
    kFdnConnectionContext = 16,
    kNumPacketTypes  // Not a real packet type, DO NOT USE
};

// A player's identity + game state, exchanged once per jack connection so a
// neighbour knows who it is plugged into. Packed: the struct layout IS the
// wire format (both ends run the same firmware). RDC never reads these fields —
// it forwards the profile to the game layer opaquely.
struct PlayerProfile {
    uint16_t userId;
    uint8_t gameRole;
    uint8_t allegiance;
    char faction[8];
    char name[16];
} __attribute__((packed));

// PDN-to-neighbour connection context. seqId is stamped by the ReliableChannel.
// chainRole is recorded by the receiver for the device chain SM (#156).
struct PdnConnectionContext {
    uint8_t seqId;
    uint8_t chainRole;
    PlayerProfile player;
} __attribute__((packed));

// FDN self-description. ponytail: fields TBD (#156-era); one reserved byte keeps
// the struct non-empty and packed until the FDN profile is designed.
struct FdnProfile {
    uint8_t reserved;
} __attribute__((packed));

// FDN-to-neighbour connection context; mirrors PdnConnectionContext.
struct FdnConnectionContext {
    uint8_t seqId;
    uint8_t chainRole;
    FdnProfile fdn;
} __attribute__((packed));

struct DataPktHdr
{
    //Total packet length including header
    uint8_t pktLen;
    PktType packetType;
} __attribute__((packed));

struct ChainConfirmPayload
{
    uint8_t originatorMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct RoleAnnouncePayload
{
    uint8_t role;               // 1 = hunter, 0 = target/bounty
    uint8_t championMac[6];
    uint8_t seqId;
} __attribute__((packed));

struct RoleAnnounceAckPayload
{
    uint8_t seqId;
} __attribute__((packed));

struct ChainGameEventAckPayload
{
    uint8_t seqId;
} __attribute__((packed));

enum class ShootoutCmd : uint8_t
{
    CONFIRM = 0,
    BRACKET = 1,
    MATCH_START = 2,
    MATCH_RESULT = 3,
    TOURNAMENT_END = 4,
    PEER_LOST = 5,
    ABORT = 6,
};

struct ShootoutPacket
{
    ShootoutCmd cmd;
    uint8_t     seqId;   // nonzero for reliable commands; 0 = no ack expected
    uint8_t     payload[];
} __attribute__((packed));

struct ShootoutAckPayload
{
    ShootoutCmd cmd;
    uint8_t     seqId;
} __attribute__((packed));
