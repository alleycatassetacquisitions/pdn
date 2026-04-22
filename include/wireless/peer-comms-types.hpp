#pragma once

#include <cstdint>

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t
{
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
    kNumPacketTypes //Not a real packet type, DO NOT USE
};

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
