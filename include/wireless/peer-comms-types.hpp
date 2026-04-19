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