#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include "wireless/peer-comms-types.hpp"

// ============================================
// Peer Comms Types Tests
// ============================================

class PeerCommsTypesTestSuite : public testing::Test {};

inline void roleAnnouncePayloadHasCorrectSize() {
    // Role (1) + Champion MAC (6) + Seq ID (1) = 8 bytes
    EXPECT_EQ(sizeof(RoleAnnouncePayload), 8);
    EXPECT_EQ(sizeof(RoleAnnouncePayload::role), 1);
    EXPECT_EQ(sizeof(RoleAnnouncePayload::championMac), 6);
    EXPECT_EQ(sizeof(RoleAnnouncePayload::seqId), 1);
}

inline void roleAnnounceAckPayloadHasCorrectSize() {
    // Seq ID (1) = 1 byte
    EXPECT_EQ(sizeof(RoleAnnounceAckPayload), 1);
    EXPECT_EQ(sizeof(RoleAnnounceAckPayload::seqId), 1);
}

inline void roleAnnouncePayloadIsPacked() {
    // Verify that padding doesn't affect struct size
    // 1 + 6 + 1 = 8 bytes should be the total
    RoleAnnouncePayload payload;
    uint8_t* ptr = (uint8_t*)&payload;

    // Set values
    payload.role = 1;
    payload.championMac[0] = 0xAA;
    payload.championMac[1] = 0xBB;
    payload.championMac[2] = 0xCC;
    payload.championMac[3] = 0xDD;
    payload.championMac[4] = 0xEE;
    payload.championMac[5] = 0xFF;
    payload.seqId = 42;

    // Verify contiguous memory layout (no padding)
    EXPECT_EQ(ptr[0], 1);     // role at offset 0
    EXPECT_EQ(ptr[1], 0xAA);  // championMac[0] at offset 1
    EXPECT_EQ(ptr[2], 0xBB);  // championMac[1] at offset 2
    EXPECT_EQ(ptr[3], 0xCC);  // championMac[2] at offset 3
    EXPECT_EQ(ptr[4], 0xDD);  // championMac[3] at offset 4
    EXPECT_EQ(ptr[5], 0xEE);  // championMac[4] at offset 5
    EXPECT_EQ(ptr[6], 0xFF);  // championMac[5] at offset 6
    EXPECT_EQ(ptr[7], 42);    // seqId at offset 7
}

inline void roleAnnounceAckPayloadIsPacked() {
    // Verify packed attribute
    RoleAnnounceAckPayload payload;
    payload.seqId = 123;

    uint8_t* ptr = (uint8_t*)&payload;
    EXPECT_EQ(ptr[0], 123);  // seqId at offset 0
}

inline void packetTypeEnumHasCorrectValues() {
    // Verify enum values are sequential
    EXPECT_EQ(static_cast<uint8_t>(PktType::kPlayerInfoBroadcast), 0);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kQuickdrawCommand), 1);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kDebugPacket), 2);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kHandshakeCommand), 3);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kChainAnnouncement), 4);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kChainAnnouncementAck), 5);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kChainGameEvent), 6);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kChainConfirm), 7);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kRoleAnnounce), 8);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kRoleAnnounceAck), 9);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kChainGameEventAck), 10);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kSymbolMatchCommand), 11);
    EXPECT_EQ(static_cast<uint8_t>(PktType::kFdnConnect), 12);
}

inline void numPacketTypesIsSequentialAfterAck() {
    // kNumPacketTypes should be one after kFdnConnect (12)
    EXPECT_EQ(static_cast<uint8_t>(PktType::kNumPacketTypes), 13);
}

inline void roleAnnouncePayloadFieldsAligned() {
    RoleAnnouncePayload payload;

    // Offset of role field
    EXPECT_EQ(offsetof(RoleAnnouncePayload, role), 0);

    // Offset of championMac field
    EXPECT_EQ(offsetof(RoleAnnouncePayload, championMac), 1);

    // Offset of seqId field
    EXPECT_EQ(offsetof(RoleAnnouncePayload, seqId), 7);
}

inline void roleAnnounceAckPayloadFieldsAligned() {
    // Offset of seqId field should be 0
    EXPECT_EQ(offsetof(RoleAnnounceAckPayload, seqId), 0);
}
