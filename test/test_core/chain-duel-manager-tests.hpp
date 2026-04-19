#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/player.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

class ChainDuelManagerTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*device.mockPeerComms, addEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, removeEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(localMac));
        ON_CALL(*device.mockPeerComms, getPeerCommsState()).WillByDefault(Return(PeerCommsState::CONNECTED));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kHandshakeCommand), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&capturedHandler),
                testing::SaveArg<2>(&capturedCtx)));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kChainAnnouncement), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&capturedChainHandler),
                testing::SaveArg<2>(&capturedChainCtx)));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kChainAnnouncementAck), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&capturedAckHandler),
                testing::SaveArg<2>(&capturedAckCtx)));

        rdc.initialize(device.wirelessManager, device.serialManager, &device);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    void deliverPacketViaRDC(int command, SerialIdentifier senderJack, int deviceType = 0) {
        SerialIdentifier receivingJack = (senderJack == SerialIdentifier::OUTPUT_JACK)
            ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
        struct RawPacket { int sendingJack; int receivingJack; int deviceType; int command; } __attribute__((packed));
        RawPacket pkt{ static_cast<int>(senderJack), static_cast<int>(receivingJack), deviceType, command };
        uint8_t dummyMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

        ASSERT_NE(capturedHandler, nullptr);
        capturedHandler(dummyMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt), capturedCtx);
    }

    void connectInputPort() {
        rdc.sync(&device);
        deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
        rdc.sync(&device);
        deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
        rdc.sync(&device);
    }

    void connectOutputPort() {
        device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
        rdc.sync(&device);
        deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
        rdc.sync(&device);
    }

    // Set up the physical hunter-champion topology (direct peers only — no
    // role state). Tests that need roles applied should call
    // applyHunterChampionRoles(cdm) afterward.
    //   OUTPUT (opponent jack) → bounty peer (opponentMac)
    //   INPUT  (supporter jack) → hunter peer (supporterMac)
    void setupHunterChampion() {
        player.setIsHunter(true);
        device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
        rdc.sync(&device);
        deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
        rdc.sync(&device);
        rdc.sync(&device);
        struct RawPacket { int sendingJack; int receivingJack; int deviceType; int command; } __attribute__((packed));
        RawPacket pkt{ static_cast<int>(SerialIdentifier::OUTPUT_JACK), static_cast<int>(SerialIdentifier::INPUT_JACK), 0, HSCommand::EXCHANGE_ID };
        capturedHandler(supporterMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt), capturedCtx);
        rdc.sync(&device);
        capturedHandler(supporterMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt), capturedCtx);
        rdc.sync(&device);
    }

    void applyHunterChampionRoles(ChainDuelManager& cdm) {
        cdm.setPeerRole(SerialIdentifier::OUTPUT_JACK, false);  // bounty opponent
        cdm.setPeerRole(SerialIdentifier::INPUT_JACK, true);    // hunter supporter
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    Player player;

    PeerCommsInterface::PacketCallback capturedHandler = nullptr;
    void* capturedCtx = nullptr;
    PeerCommsInterface::PacketCallback capturedChainHandler = nullptr;
    void* capturedChainCtx = nullptr;
    PeerCommsInterface::PacketCallback capturedAckHandler = nullptr;
    void* capturedAckCtx = nullptr;
    uint8_t localMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t opponentMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t supporterMac[6] = {0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
};

// Role derivation: champion topology produces correct is* / canInitiate / peers results.
// Without peers everything returns false/empty; with a full champion setup everything flips.
inline void cdmRoleDerivationWithChampionTopology(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // No peers: default-champion (isSupporter is false, no opponent peer).
    EXPECT_TRUE(cdm.isChampion());
    EXPECT_FALSE(cdm.isSupporter());
    EXPECT_FALSE(cdm.canInitiateMatch());
    EXPECT_TRUE(cdm.getSupporterChainPeers().empty());

    suite->setupHunterChampion();
    ChainDuelManager cdm2(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm2);

    EXPECT_TRUE(cdm2.isChampion());
    EXPECT_FALSE(cdm2.isSupporter());
    EXPECT_TRUE(cdm2.canInitiateMatch());
    EXPECT_FALSE(cdm2.getSupporterChainPeers().empty());
}

// Bounties never initiate matches regardless of topology
inline void cdmCanInitiateMatchFalseForBounty(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(false);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_FALSE(cdm.canInitiateMatch());
}

// Confirm lifecycle: starts at 0, increments per unique MAC, dedup blocks repeats, clear resets
inline void cdmConfirmLifecycle(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    ASSERT_TRUE(cdm.isChampion());

    EXPECT_EQ(cdm.getBoostMs(), 0u);

    // New signature: (fromMac, originatorMac, seqId).
    // For 2-device case, fromMac == originatorMac (direct delivery, no forwarder).
    cdm.onConfirmReceived(suite->supporterMac, suite->supporterMac, 1);
    EXPECT_EQ(cdm.getBoostMs(), 15u);
    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 1u);

    // Same originator, different seqId → still same MAC, still deduped at champion.
    cdm.onConfirmReceived(suite->supporterMac, suite->supporterMac, 2);
    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 1u);

    cdm.clearSupporterConfirms();
    EXPECT_EQ(cdm.getBoostMs(), 0u);
    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 0u);
}

// 10. onChainStateChanged clears confirms when chain drains to 0
inline void cdmOnChainStateChangedClearsOnDrain(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    ASSERT_TRUE(cdm.isChampion());

    cdm.onConfirmReceived(suite->supporterMac, suite->supporterMac, 1);
    ASSERT_EQ(cdm.getBoostMs(), 15u);

    // First call: chain has peers (lastSupporterChainCount_ goes from 0 to N)
    cdm.onChainStateChanged();
    EXPECT_EQ(cdm.getBoostMs(), 15u);

    // Disconnect the input port by advancing time past heartbeat timeout
    suite->fakeClock->advance(5000);
    suite->rdc.sync(&suite->device);

    // Second call: chain drained to 0
    cdm.onChainStateChanged();
    EXPECT_EQ(cdm.getBoostMs(), 0u);
    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 0u);
}

// Confirm with originator MAC not present in the champion's supporter-jack
// chain must be rejected. Covers MAC-spoof injection by an in-range stranger.
inline void cdmConfirmFromUnknownOriginatorRejected(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    ASSERT_TRUE(cdm.isChampion());

    uint8_t stranger[6] = {0xBA, 0xAD, 0xF0, 0x0D, 0x00, 0x00};
    cdm.onConfirmReceived(suite->supporterMac, stranger, 1);
    EXPECT_EQ(cdm.getBoostMs(), 0u);
    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 0u);
}

// Middle-of-chain device (same-role peer on opponent jack) is NOT champion.
inline void cdmIsChampionFalseWithSameRoleOpponent(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Same-role peer on opponent jack → supporter, not champion.
    cdm.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);  // same-role hunter
    EXPECT_TRUE(cdm.isSupporter());
    EXPECT_FALSE(cdm.isChampion());
}

// Ring topology (same MAC reachable via both jacks): isChampion must return false.
inline void cdmIsChampionFalseInRing(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    // supporterMac is already the direct peer on INPUT_JACK (supporter jack).
    // Announce it as also reachable via OUTPUT_JACK (opponent jack) to create
    // the cross-port overlap that isLoop() detects.
    std::array<uint8_t, 6> supporterArr;
    memcpy(supporterArr.data(), suite->supporterMac, 6);
    suite->rdc.onChainAnnouncementReceived(
        suite->opponentMac,
        SerialIdentifier::OUTPUT_JACK,
        {supporterArr});

    ASSERT_TRUE(cdm.isLoop());
    EXPECT_FALSE(cdm.isChampion());
}

// sendConfirm targets championMac_ directly with a ChainConfirmPayload.
inline void cdmSendConfirmTargetsChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Seed championMac via announce.
    uint8_t champion[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);

    std::array<uint8_t, 6> target{};
    ChainConfirmPayload captured{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
        .WillOnce([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            memcpy(target.data(), mac, 6);
            memcpy(&captured, data, sizeof(captured));
            return 1;
        });

    cdm.sendConfirm();

    EXPECT_EQ(memcmp(target.data(), champion, 6), 0);
    EXPECT_EQ(memcmp(captured.originatorMac, suite->localMac, 6), 0);
}

// sendConfirm increments seqId on each call.
inline void cdmSendConfirmIncrementsSeqId(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t champion[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);

    std::vector<uint8_t> seqIds;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t* data, const size_t) {
            ChainConfirmPayload p; memcpy(&p, data, sizeof(p));
            seqIds.push_back(p.seqId);
            return 1;
        });

    cdm.sendConfirm();
    cdm.sendConfirm();
    cdm.sendConfirm();
    ASSERT_EQ(seqIds.size(), 3u);
    EXPECT_EQ(seqIds[0], 1u);
    EXPECT_EQ(seqIds[1], 2u);
    EXPECT_EQ(seqIds[2], 3u);
}

// sendConfirm is a noop when championMac_ is not set.
inline void cdmSendConfirmNoopWhenChampionMacInvalid(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // No announce received; championMac_ is nullopt.

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, _)).Times(0);

    cdm.sendConfirm();
}


// onRoleAnnounceReceived updates peerRoleByPort_ and championMac_, acks sender,
// registers champion as ESP-NOW peer.
inline void cdmRoleAnnounceUpdatesChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t championMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    bool ackSent = false;
    bool peerRegistered = false;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, sizeof(RoleAnnounceAckPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            ackSent = true;
            EXPECT_EQ(memcmp(mac, suite->opponentMac, 6), 0);
            RoleAnnounceAckPayload p;
            memcpy(&p, data, sizeof(p));
            EXPECT_EQ(p.seqId, 7u);
            return 1;
        });
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_))
        .WillRepeatedly([&](const uint8_t* m) {
            if (memcmp(m, championMac, 6) == 0) peerRegistered = true;
            return 0;
        });
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));

    cdm.onRoleAnnounceReceived(suite->opponentMac, /*role=hunter*/1, championMac, 7);

    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), championMac, 6), 0);
    EXPECT_TRUE(ackSent);
    EXPECT_TRUE(peerRegistered);
}

// Receiving announce with same championMac doesn't trigger cascade.
inline void cdmRoleAnnounceNoCascadeIfChampionUnchanged(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t championMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    // First receive triggers a cascade broadcast (championMac is new); may send to
    // both jacks so we allow any number >= 1.
    int firstCascadeCount = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
            firstCascadeCount++;
            return 1;
        });
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, championMac, 1);
    EXPECT_GE(firstCascadeCount, 1);

    // Second receive with same championMac — assert no kRoleAnnounce emit (cascade).
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).Times(0);

    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, championMac, 2);
}

// broadcastRoleAndChampion sends to supporter-jack direct peer with current
// role and championMac.
inline void cdmBroadcastRoleAndChampionSends(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    // Force championMac_ by receiving an announce.
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);
    // broadcastRoleAndChampion was already called internally by onRoleAnnounceReceived.
    // Here we just verify the last broadcast had correct content by calling again.

    bool sentToSupporter = false;
    RoleAnnouncePayload capturedFromSupporter{};
    // broadcastRoleAndChampion may send to both jacks; capture the supporter-jack send.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                sentToSupporter = true;
                memcpy(&capturedFromSupporter, data, sizeof(capturedFromSupporter));
            }
            return 1;
        });

    cdm.broadcastRoleAndChampion();

    // supporter jack for hunter = INPUT_JACK; setupHunterChampion puts
    // supporterMac on INPUT.
    EXPECT_TRUE(sentToSupporter);
    EXPECT_EQ(capturedFromSupporter.role, 1u);
    EXPECT_EQ(memcmp(capturedFromSupporter.championMac, champion, 6), 0);
}

// Ack with matching seqId clears pending; subsequent sync does not retransmit.
inline void cdmAckClearsPending(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);

    // Capture the seqId from the supporter-jack send (that's what pending_ tracks).
    uint8_t seqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                RoleAnnouncePayload p; memcpy(&p, data, sizeof(p));
                seqId = p.seqId;
            }
            return 1;
        });
    cdm.broadcastRoleAndChampion();
    ASSERT_NE(seqId, 0u);

    // Ack with matching seqId clears pending.
    cdm.onRoleAnnounceAckReceived(suite->supporterMac, seqId);

    // Subsequent sync() after timeout must not emit any further role announces.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).Times(0);
    suite->fakeClock->advance(150);
    cdm.sync();
}

// Ack with matching seqId but wrong fromMac must not clear pending.
inline void cdmAckFromWrongMacIgnored(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);

    uint8_t seqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                RoleAnnouncePayload p; memcpy(&p, data, sizeof(p));
                seqId = p.seqId;
            }
            return 1;
        });
    cdm.broadcastRoleAndChampion();
    ASSERT_NE(seqId, 0u);

    // Forged ack: right seqId, wrong source MAC. Must be ignored.
    uint8_t forgedMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00};
    cdm.onRoleAnnounceAckReceived(forgedMac, seqId);

    // sync() after timeout must still retransmit to supporterMac.
    int retransmits = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) retransmits++;
            return 1;
        });
    suite->fakeClock->advance(150);
    cdm.sync();
    EXPECT_EQ(retransmits, 1);
}

// Retransmit abandons after kMaxRetries with no ack.
inline void cdmRetransmitAbandonsAfterMax(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);

    int supporterSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            // Count sends to the supporter-jack peer (the one that goes into pending_).
            if (memcmp(mac, suite->supporterMac, 6) == 0) supporterSends++;
            return 1;
        });

    cdm.broadcastRoleAndChampion();  // initial: 1 to supporter + 1 fire-and-forget to opponent
    ASSERT_EQ(supporterSends, 1);

    // Advance clock + sync 3 times → 3 retransmits (supporter only, pending path).
    // Exponential backoff: waits are 100, 200, 400ms. Advance past the biggest
    // per iter to guarantee each retry fires.
    for (int i = 0; i < 3; i++) {
        suite->fakeClock->advance(500);
        cdm.sync();
    }
    EXPECT_EQ(supporterSends, 4);  // 1 initial + 3 retransmits

    // 4th sync should not emit — pending abandoned.
    suite->fakeClock->advance(500);
    cdm.sync();
    EXPECT_EQ(supporterSends, 4);
}

// RetryStats counts sends, retries, acks, and abandons across a role-announce
// lifecycle. Observability for hardware-validation tuning.
inline void cdmRetryStatsRecordsLifecycle(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));

    uint8_t seqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                RoleAnnouncePayload p; memcpy(&p, data, sizeof(p));
                seqId = p.seqId;
            }
            return 1;
        });

    // Initial announce triggers one send, then an ack clears it.
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);
    ASSERT_NE(seqId, 0u);
    suite->fakeClock->advance(50);
    cdm.onRoleAnnounceAckReceived(suite->supporterMac, seqId);

    auto s1 = cdm.getRetryStats();
    EXPECT_GE(s1.sends, 1u);
    EXPECT_EQ(s1.abandons, 0u);
    EXPECT_GE(s1.ackCount, 1u);
    EXPECT_GT(s1.ackLatencyMsSum, 0u);

    // Force a retransmit then exhaust retries → abandons increments.
    // Advance generously per iter to cover exponential backoff (max 1600ms).
    cdm.broadcastRoleAndChampion();
    for (int i = 0; i < 7; i++) {
        suite->fakeClock->advance(2000);
        cdm.sync();
    }
    auto s2 = cdm.getRetryStats();
    EXPECT_GE(s2.retries, 3u);
    EXPECT_GE(s2.abandons, 1u);
}

// Becoming champion self-assigns championMac to own MAC.
inline void cdmOnChainStateBecomesChampionSetsSelfMac(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Solo device — isChampion() is true by default under current semantics.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    cdm.onChainStateChanged();

    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), suite->localMac, 6), 0);
}

// A supporter that inherited championMac from an upstream announce keeps
// that cached value across onChainStateChanged; only a self-MAC value is
// cleared (see cdmChampionToSupporterClearsStaleSelfMac).
inline void cdmSupporterKeepsUpstreamChampionMacAfterTransition(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);

    uint8_t champion[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    // Same-role opponent on OUTPUT, same-role supporter on INPUT → isSupporter.
    cdm.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);
    cdm.setPeerRole(SerialIdentifier::INPUT_JACK, true);
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    ASSERT_TRUE(cdm.isSupporter());
    ASSERT_FALSE(cdm.isChampion());

    // onChainStateChanged when still supporter: championMac_ remains unchanged.
    cdm.onChainStateChanged();
    EXPECT_NE(cdm.getChampionMac(), nullptr);
}

// onChainStateChanged fires broadcast ONCE when a new supporter-jack peer appears,
// and does NOT re-fire on subsequent calls with the same peer.
inline void cdmOnChainStateNewSupporterTriggersBroadcast(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    // First call: device is champion (self-assign), supporter-jack peer present →
    // should broadcast exactly once to supporter-jack.
    int firstCallSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) firstCallSends++;
            return 1;
        });
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    cdm.onChainStateChanged();
    ASSERT_TRUE(cdm.isChampion());
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(firstCallSends, 1);

    // Second call: supporter-jack peer unchanged — must NOT re-broadcast.
    int secondCallSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) secondCallSends++;
            return 1;
        });
    cdm.onChainStateChanged();
    EXPECT_EQ(secondCallSends, 0);
}

// End-to-end: Champion A receives a direct confirm from the originator S2
// after the join-announce cascade has propagated championMac=A to S2.
// Drives S1 (the middle supporter) to cascade the announce, and S2 to
// target A directly.
inline void chainDuelThreeDeviceConfirm(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    uint8_t macA[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    // Leg 1: S1 (middle supporter) receives announce from A, updates championMac_,
    // and cascades to its supporter-jack child.
    ChainDuelManager s1(&suite->player, suite->device.wirelessManager, &suite->rdc);
    s1.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);
    s1.setPeerRole(SerialIdentifier::INPUT_JACK, true);

    std::vector<std::array<uint8_t, 6>> s1Cascade;
    RoleAnnouncePayload s1CascadePayload{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            std::array<uint8_t, 6> m; memcpy(m.data(), mac, 6);
            s1Cascade.push_back(m);
            memcpy(&s1CascadePayload, data, sizeof(s1CascadePayload));
            return 1;
        });

    s1.onRoleAnnounceReceived(suite->opponentMac, 1, macA, 1);
    ASSERT_GE(s1Cascade.size(), 1u);
    // S1's cascade target is its supporter-jack direct peer (supporterMac in fixture).
    EXPECT_EQ(memcmp(s1Cascade[0].data(), suite->supporterMac, 6), 0);
    EXPECT_EQ(memcmp(s1CascadePayload.championMac, macA, 6), 0);

    // Leg 2: S2 (end of chain) receives the cascaded announce, caches championMac=A.
    // In hardware, S1's MAC would be S2's opponent-jack direct peer. In this shared
    // fixture, S2's opponent-jack (OUTPUT) direct peer is opponentMac, so the announce
    // arrives from opponentMac (standing in for S1 in this single-RDC test).
    ChainDuelManager s2(&suite->player, suite->device.wirelessManager, &suite->rdc);
    s2.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);

    s2.onRoleAnnounceReceived(suite->opponentMac, 1, macA, s1CascadePayload.seqId);
    ASSERT_NE(s2.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(s2.getChampionMac(), macA, 6), 0);

    // Leg 3: S2 presses button → confirm targets A directly.
    std::array<uint8_t, 6> confirmTarget{};
    ChainConfirmPayload confirmPayload{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
        .WillOnce([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            memcpy(confirmTarget.data(), mac, 6);
            memcpy(&confirmPayload, data, sizeof(confirmPayload));
            return 1;
        });

    s2.sendConfirm();

    EXPECT_EQ(memcmp(confirmTarget.data(), macA, 6), 0);  // direct to A
    EXPECT_EQ(memcmp(confirmPayload.originatorMac, suite->localMac, 6), 0);

    // Leg 4: Champion A receives and records.
    // Use a fresh "distant supporter" MAC since the shared fixture's self-MAC
    // (localMac) cannot appear in the daisy-chain — RDC filters self-MAC out.
    // Seed the chain via an announcement from supporterMac (the direct peer).
    // The RDC re-emits a chain announcement to the opposite jack after state
    // change, so allow that send.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainAnnouncement, _, _)).WillRepeatedly(Return(1));
    uint8_t distantMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    std::array<uint8_t, 6> distantArr;
    memcpy(distantArr.data(), distantMac, 6);
    suite->rdc.onChainAnnouncementReceived(
        suite->supporterMac, SerialIdentifier::INPUT_JACK, {distantArr});

    ChainDuelManager a(&suite->player, suite->device.wirelessManager, &suite->rdc);
    a.setPeerRole(SerialIdentifier::OUTPUT_JACK, false);
    a.setPeerRole(SerialIdentifier::INPUT_JACK, true);
    ASSERT_TRUE(a.isChampion());

    a.onConfirmReceived(suite->supporterMac, distantMac, confirmPayload.seqId);
    EXPECT_EQ(a.getBoostMs(), 15u);
}

// broadcastRoleAndChampion (or its replacement) sends a full RoleAnnouncePayload
// to the opponent-jack direct peer so that peer can learn our role.
// This covers the C1 regression: broadcastLocalRole sent a 1-byte payload that
// onRoleAnnouncePacket (now requiring 8 bytes) silently dropped.
inline void cdmBroadcastToOpponentJackPopulatesRemoteRole(ChainDuelManagerTests* suite) {
    // Hunter champion: opponent jack = OUTPUT, supporter jack = INPUT.
    suite->player.setIsHunter(true);
    suite->setupHunterChampion();

    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // Allow any sends triggered inside onChainStateChanged / broadcastRoleAndChampion.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));

    // Capture any kRoleAnnounce sent to the opponent-jack direct peer (opponentMac).
    bool sentToOpponent = false;
    RoleAnnouncePayload capturedPayload{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->opponentMac, 6) == 0) {
                sentToOpponent = true;
                memcpy(&capturedPayload, data, sizeof(capturedPayload));
            }
            return 1;
        });

    // onChainStateChanged self-assigns champion and should broadcast to both jacks.
    cdm.onChainStateChanged();

    EXPECT_TRUE(sentToOpponent) << "No 8-byte RoleAnnouncePayload sent to opponent-jack peer";
    if (sentToOpponent) {
        EXPECT_EQ(capturedPayload.role, 1u);  // hunter
        EXPECT_EQ(memcmp(capturedPayload.championMac, suite->localMac, 6), 0);
    }
}

// When a device was champion (championMac_ == self-MAC) and gains a same-role
// peer on its opponent jack (becoming a supporter), onChainStateChanged must
// clear championMac_ rather than cascade the stale self-MAC to its supporter.
inline void cdmChampionToSupporterClearsStaleSelfMac(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // Allow all sends; connectOutputPort triggers handshake sends too.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    // Start as solo champion — self-assigns championMac_ to localMac.
    cdm.onChainStateChanged();
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), suite->localMac, 6), 0);

    // Connect output port so there is a direct peer on opponent jack.
    suite->connectOutputPort();
    // Mark that peer as same-role (hunter) → device becomes supporter.
    cdm.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);
    ASSERT_TRUE(cdm.isSupporter());

    // onChainStateChanged should detect we hold stale self-MAC and clear it.
    cdm.onChainStateChanged();

    EXPECT_EQ(cdm.getChampionMac(), nullptr)
        << "Stale self-MAC was not cleared when champion was demoted to supporter";
}

// Role announce received on supporter-jack (child direction) updates role
// but does NOT update championMac_. Prevents ping-pong between same-role peers.
inline void cdmRoleAnnounceFromSupporterJackIgnoresChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Seed championMac via opponent-jack announce first.
    uint8_t realChampion[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, realChampion, 1);
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), realChampion, 6), 0);

    // Now a supporter-jack peer sends an announce with a different championMac
    // (simulating the ping-pong scenario). peerRoleByPort for supporter-jack
    // should update, but championMac_ should NOT change.
    uint8_t wrongChampion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    cdm.onRoleAnnounceReceived(suite->supporterMac, 1, wrongChampion, 2);

    // championMac_ unchanged
    EXPECT_EQ(memcmp(cdm.getChampionMac(), realChampion, 6), 0);
}

// Role announce from opposite-role opponent-jack peer does NOT update championMac_.
inline void cdmRoleAnnounceFromOppositeRoleOpponentIgnoresChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t selfMacArr[6];
    memcpy(selfMacArr, suite->localMac, 6);

    // Seed championMac_ to self via onChainStateChanged (champion self-assign).
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    cdm.onChainStateChanged();
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), selfMacArr, 6), 0);

    // Receive announce from bounty (role=0) on opponent jack. Hunter champion
    // should ignore this — opposite-role sender is a duel opponent, not a chain parent.
    uint8_t bountyChampion[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    cdm.onRoleAnnounceReceived(suite->opponentMac, /*role=bounty*/0, bountyChampion, 7);

    // championMac_ must remain self (not overwritten with opponent's champion MAC).
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), selfMacArr, 6), 0);
}

// After chain break (S1 disconnects from A), S1 becomes champion
// and updates championMac_ to self.
inline void chainDuelReconfigRecovers(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    uint8_t macA[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    // S1 starts with championMac=A (received from parent).
    ChainDuelManager s1(&suite->player, suite->device.wirelessManager, &suite->rdc);
    s1.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);
    s1.setPeerRole(SerialIdentifier::INPUT_JACK, true);

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounceAck, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kHandshakeCommand, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));

    s1.onRoleAnnounceReceived(suite->opponentMac, 1, macA, 1);
    ASSERT_NE(s1.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(s1.getChampionMac(), macA, 6), 0);

    // Simulate S1 losing opponent-jack peer by advancing the fake clock past
    // the heartbeat timeout and calling rdc.sync to trigger the disconnect path.
    suite->fakeClock->advance(5000);
    suite->rdc.sync(&suite->device);
    // Now opponent-jack peer is gone; re-evaluate on CDM.
    s1.onChainStateChanged();

    ASSERT_TRUE(s1.isChampion());
    ASSERT_NE(s1.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(s1.getChampionMac(), suite->localMac, 6), 0);
}

// COUNTDOWN is fire-and-forget: seqId must be 0 and no retry must fire.
inline void cdmGameEventCountdownIsFireAndForget(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    int countdownSends = 0;
    uint8_t seqId = 0xAB;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t* data, const size_t) {
            ChainGameEventPayload p; memcpy(&p, data, sizeof(p));
            if (p.event_type == (uint8_t)ChainGameEventType::COUNTDOWN) {
                seqId = p.seqId;
                countdownSends++;
            }
            return 1;
        });
    cdm.sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);
    EXPECT_EQ(countdownSends, 1);
    EXPECT_EQ(seqId, 0u);  // fire-and-forget sentinel

    // sync() after timeout must not retransmit a COUNTDOWN.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, _)).Times(0);
    suite->fakeClock->advance(1000);
    cdm.sync();
}

// WIN is tracked: seqId != 0, pending registered, retransmit fires on timer.
inline void cdmGameEventWinIsTrackedAndRetried(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    int supporterSends = 0;
    uint8_t winSeqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                ChainGameEventPayload p; memcpy(&p, data, sizeof(p));
                if (p.event_type == (uint8_t)ChainGameEventType::WIN) {
                    winSeqId = p.seqId;
                    supporterSends++;
                }
            }
            return 1;
        });

    cdm.sendGameEventToSupporters(ChainGameEventType::WIN);
    EXPECT_EQ(supporterSends, 1);
    ASSERT_NE(winSeqId, 0u);

    // Advance past first timeout (100ms), sync() should retransmit once.
    suite->fakeClock->advance(150);
    cdm.sync();
    EXPECT_EQ(supporterSends, 2);
}

// ACK clears pending: no further retransmits after a matching ACK.
inline void cdmGameEventAckClearsPending(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    uint8_t winSeqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                ChainGameEventPayload p; memcpy(&p, data, sizeof(p));
                if (p.event_type == (uint8_t)ChainGameEventType::LOSS) winSeqId = p.seqId;
            }
            return 1;
        });

    cdm.sendGameEventToSupporters(ChainGameEventType::LOSS);
    ASSERT_NE(winSeqId, 0u);

    cdm.onChainGameEventAckReceived(suite->supporterMac, winSeqId);

    // After ACK, sync past timeout must not retransmit.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, _)).Times(0);
    suite->fakeClock->advance(1000);
    cdm.sync();
}

// After kMaxRetries with no ACK, pending is abandoned — no further sends.
inline void cdmGameEventAbandonsAfterMax(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    int supporterSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) supporterSends++;
            return 1;
        });

    cdm.sendGameEventToSupporters(ChainGameEventType::WIN);
    ASSERT_EQ(supporterSends, 1);

    // kMaxRetries = 3. Advance past each exponential backoff window (100, 200,
    // 400, 800ms). Use 1000ms to cover the largest.
    for (int i = 0; i < 3; i++) {
        suite->fakeClock->advance(1000);
        cdm.sync();
    }
    EXPECT_EQ(supporterSends, 4);  // 1 initial + 3 retransmits

    // After abandon, further sync must not retransmit.
    suite->fakeClock->advance(1000);
    cdm.sync();
    EXPECT_EQ(supporterSends, 4);

    EXPECT_EQ(cdm.getRetryStats().abandons, 1u);
}

