#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "rdc-hello-tests.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/player.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

// Physical connectivity here is the production HELLO path: framed HELLO bytes
// pushed into a native jack driver, drained by its exec() pump into the RDC's
// parser, then the ESP-NOW context exchange completed to commit the link.
class ChainDuelManagerTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        wireRadioDefaults(device, localMac);
        device.serialManager->setOutputJack(&outJack);
        device.serialManager->setInputJack(&inJack);
        ON_CALL(*device.mockPeerComms,
                setPacketHandler(testing::Eq(PktType::kPdnConnectionContext), _, _))
            .WillByDefault(testing::DoAll(testing::SaveArg<1>(&contextHandler),
                                          testing::SaveArg<2>(&contextCtx)));
        // Role announces have no reply packet: the radio's send result is the
        // delivery signal, so tests drive it through this captured handler.
        ON_CALL(*device.mockPeerComms,
                setSendStatusHandler(testing::Eq(PktType::kRoleAnnounce), _, _))
            .WillByDefault(testing::DoAll(testing::SaveArg<1>(&roleAnnounceSendStatus),
                                          testing::SaveArg<2>(&roleAnnounceSendStatusCtx)));

        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);

        // RDC traffic (context exchange, roster announces) shares this mock with
        // the per-test game-packet expectations. A catch-all declared here keeps
        // those sends from reading as unexpected; gmock still prefers the
        // narrower expectations a test declares afterwards.
        EXPECT_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /// Feeds a peer's PdnConnectionContext in through the handler the reliable
    /// transport registered with the radio driver — the production receive path.
    void deliverPdnContext(const uint8_t* peerMac) {
        if (contextHandler == nullptr) return;
        std::vector<uint8_t> bytes = pdnContextBytes(/*chainRole=*/0, /*userId=*/4242,
                                                     ++contextSeqId);
        contextHandler(peerMac, bytes.data(), bytes.size(), contextCtx);
    }

    /// Brings `jack` from Idle to Connected against `peerMac`, optionally
    /// carrying an advertised chain head.
    void connectJackTo(NativeSerialDriver& jack, const uint8_t* peerMac,
                       const uint8_t* advertisedHead = nullptr) {
        deliverFrame(jack, chainHelloFrame(peerMac, advertisedHead));
        rdc.sync(&device);
        deliverPdnContext(peerMac);
        rdc.sync(&device);
    }

    /// Only the peer's first HELLO: the jack holds its MAC but the exchange has
    /// not completed, so it stays CONNECTING.
    void beginConnectJackTo(NativeSerialDriver& jack, const uint8_t* peerMac) {
        deliverFrame(jack, chainHelloFrame(peerMac, nullptr));
        rdc.sync(&device);
    }

    /// Brings the supporter-side jack up against supporterMac.
    void connectInputPort() { connectJackTo(inJack, supporterMac); }

    /// Brings the opponent-side jack up against opponentMac.
    void connectOutputPort() { connectJackTo(outJack, opponentMac); }

    // Set up the physical hunter-champion topology (direct peers only — no
    // role state). Tests that need roles applied should call
    // applyHunterChampionRoles(cdm) afterward.
    //   OUTPUT (opponent jack) → bounty peer (opponentMac)
    //   INPUT  (supporter jack) → hunter peer (supporterMac)
    void setupHunterChampion() {
        player.setIsHunter(true);
        connectOutputPort();
        connectInputPort();
    }

    /// Closes a ring around this device: it heads a chain out of OUTPUT and its
    /// own MAC comes back on INPUT, which is the only local evidence of closure.
    void closeRingAroundSelf() {
        connectOutputPort();
        connectJackTo(inJack, supporterMac, localMac);
    }

    void applyHunterChampionRoles(ChainDuelManager& cdm) {
        cdm.setPeerRole(SerialIdentifier::OUTPUT_JACK, false);  // bounty opponent
        cdm.setPeerRole(SerialIdentifier::INPUT_JACK, true);    // hunter supporter
    }

    MockDevice device;
    NativeSerialDriver outJack{"cdm-out"};
    NativeSerialDriver inJack{"cdm-in"};
    // Declared after the jacks so it is destroyed first; its dtor clears their
    // byte callbacks, mirroring production where the drivers outlive the RDC.
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    Player player;

    /// Replays a radio send result for a role announce, the way the driver
    /// would after the frame goes out. success=false is SEND_FAIL.
    void deliverRoleAnnounceSendResult(const uint8_t* toMac, uint8_t seqId, bool success) {
        if (roleAnnounceSendStatus == nullptr) return;
        RoleAnnouncePayload echoed{};
        echoed.seqId = seqId;
        roleAnnounceSendStatus(toMac, reinterpret_cast<const uint8_t*>(&echoed),
                               sizeof(echoed), success, roleAnnounceSendStatusCtx);
    }

    PeerCommsInterface::PacketCallback contextHandler = nullptr;
    void* contextCtx = nullptr;
    PeerCommsInterface::SendStatusCallback roleAnnounceSendStatus = nullptr;
    void* roleAnnounceSendStatusCtx = nullptr;
    uint8_t contextSeqId = 0;
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

// Half-open gate (#162): a jack that only reached CONNECTING knows its peer MAC
// from one serial frame but has no proven return path, and a match pushed across
// it strands the initiator waiting for an ack that never comes. Only a CONNECTED
// opponent jack opens the gate.
inline void cdmCanInitiateMatchRequiresConnectedOpponentJack(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // One HELLO: the OUTPUT jack knows the peer MAC, but the context exchange
    // has not answered, so the connection is still half-open.
    suite->beginConnectJackTo(suite->outJack, suite->opponentMac);
    cdm.setPeerRole(SerialIdentifier::OUTPUT_JACK, false);  // bounty opponent

    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTING);
    EXPECT_FALSE(cdm.canInitiateMatch());

    // The peer's context lands, proving a path back.
    suite->deliverPdnContext(suite->opponentMac);
    suite->rdc.sync(&suite->device);

    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
    ASSERT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::PDN);
    EXPECT_TRUE(cdm.canInitiateMatch());
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

    // First call: chain has peers (lastSupporterChainCount goes from 0 to N)
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

// Inside a closed ring nobody is a champion: the shootout owns that topology.
inline void cdmIsChampionFalseInRing(ChainDuelManagerTests* suite) {
    suite->closeRingAroundSelf();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    ASSERT_TRUE(cdm.isLoop());
    EXPECT_FALSE(cdm.isChampion());
}

// sendConfirm targets championMac directly with a ChainConfirmPayload.
inline void cdmSendConfirmTargetsChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Seed championMac via announce.
    uint8_t champion[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
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

// sendConfirm is a noop when championMac is not set.
inline void cdmSendConfirmNoopWhenChampionMacInvalid(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // No announce received; championMac is nullopt.

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, _)).Times(0);

    cdm.sendConfirm();
}

// A confirm can outrun the join that puts its originator in the champion's
// supporter chain. Held rather than dropped, and admitted the moment the join
// lands — otherwise that supporter contributes nothing all round.
inline void cdmConfirmBufferedUntilOriginatorJoinsChain(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    ASSERT_TRUE(cdm.isChampion());

    uint8_t multiHopMac[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    cdm.onConfirmReceived(suite->supporterMac, multiHopMac, 1);
    ASSERT_EQ(cdm.getConfirmedSupporterCount(), 0u);

    // The join lands: multiHopMac sits somewhere behind the direct supporter-jack
    // peer and has just named this device as its champion.
    cdm.onChainJoinReceived(multiHopMac, suite->localMac);

    cdm.onChainStateChanged();

    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 1u);
    EXPECT_EQ(cdm.getBoostMs(), ChainDuelManager::BOOST_PER_SUPPORTER_MS);
}

// Saturating the confirm slots with strangers must not silence a real supporter.
// The slots are fixed and the packet handler cannot prove an originator is a
// member, so anything on the channel can fill them; refusing new entries when
// full, or holding rejected entries forever, would turn that into a mute button
// for the whole round.
inline void cdmStrangerConfirmsCannotSilenceRealSupporter(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    ASSERT_TRUE(cdm.isChampion());

    // Twice the slot count, none of them ever in the roster.
    for (int i = 0; i < 36; i++) {
        uint8_t stranger[6] = {0xEE, 0xEE, 0xEE, 0xEE,
                               static_cast<uint8_t>(i), static_cast<uint8_t>(i)};
        cdm.onConfirmReceived(suite->supporterMac, stranger, 1);
    }
    ASSERT_EQ(cdm.getConfirmedSupporterCount(), 0u);

    // A genuine multi-hop supporter presses afterwards and joins the roster.
    uint8_t realMac[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    cdm.onConfirmReceived(suite->supporterMac, realMac, 1);
    cdm.onChainJoinReceived(realMac, suite->localMac);
    cdm.onChainStateChanged();

    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 1u);
    EXPECT_EQ(cdm.getBoostMs(), ChainDuelManager::BOOST_PER_SUPPORTER_MS);
}

// A join is a MAC claiming chain membership, so it has to name the champion it
// claims to follow. Chains run side by side in one room and every frame is in
// range of all of them; without the check, one chain's supporter would enrol
// itself in another's roster and buy that champion boost it never earned.
inline void cdmChainJoinForAnotherChampionIsIgnored(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    ASSERT_TRUE(cdm.isChampion());

    uint8_t otherChampion[6] = {0x99, 0x99, 0x99, 0x99, 0x99, 0x99};
    uint8_t foreignSupporter[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    cdm.onChainJoinReceived(foreignSupporter, otherChampion);
    cdm.onConfirmReceived(foreignSupporter, foreignSupporter, 1);

    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 0u);
    EXPECT_EQ(cdm.getBoostMs(), 0u);

    // The same device, now naming us, is admitted — the MAC was never the issue.
    cdm.onChainJoinReceived(foreignSupporter, suite->localMac);
    EXPECT_EQ(cdm.getConfirmedSupporterCount(), 1u);
}

// Head transfer swaps the champion under a supporter that already pressed. The
// standing confirm has to follow it or the press buys no boost.
inline void cdmConfirmResentWhenChampionChanges(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _))
        .WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).WillRepeatedly(Return(0));

    std::vector<std::array<uint8_t, 6>> confirmTargets;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            std::array<uint8_t, 6> target;
            memcpy(target.data(), mac, 6);
            confirmTargets.push_back(target);
            return 1;
        });

    uint8_t firstChampion[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, firstChampion, 1);
    ASSERT_TRUE(cdm.isSupporter());
    cdm.sendConfirm();

    uint8_t secondChampion[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, secondChampion, 2);

    ASSERT_EQ(confirmTargets.size(), 2u);
    EXPECT_EQ(memcmp(confirmTargets[0].data(), firstChampion, 6), 0);
    EXPECT_EQ(memcmp(confirmTargets[1].data(), secondChampion, 6), 0);
}

// COUNTDOWN wipes the champion's roll call, so the press that preceded it is
// spent. A champion change after one must not resurrect it.
inline void cdmCountdownVoidsStandingConfirm(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _))
        .WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).WillRepeatedly(Return(0));

    int confirmsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, _))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
            confirmsSent++;
            return 1;
        });

    uint8_t firstChampion[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, firstChampion, 1);
    ASSERT_TRUE(cdm.isSupporter());
    cdm.sendConfirm();
    ASSERT_EQ(confirmsSent, 1);

    cdm.onChainGameEventReceived(static_cast<uint8_t>(ChainGameEventType::COUNTDOWN));

    uint8_t secondChampion[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, secondChampion, 2);

    EXPECT_EQ(confirmsSent, 1);
}

// Unplugging ends the round for this supporter. The press it made must not
// follow it into whatever chain it is patched into next.
inline void cdmSupporterRoleLossVoidsStandingConfirm(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Tearing the links down makes the RDC emit its own traffic; a catch-all
    // fallback keeps that off the confirm counter below.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).WillRepeatedly(Return(0));

    int confirmsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, _))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
            confirmsSent++;
            return 1;
        });

    uint8_t champion[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);
    ASSERT_TRUE(cdm.isSupporter());
    cdm.sendConfirm();
    ASSERT_EQ(confirmsSent, 1);

    // Cable out on both jacks: the heartbeat lapses and the direct peers go.
    suite->fakeClock->advance(5000);
    suite->rdc.sync(&suite->device);
    cdm.onChainStateChanged();
    ASSERT_FALSE(cdm.isSupporter());

    cdm.resendConfirm();
    EXPECT_EQ(confirmsSent, 1);
}

// The role edge reaches the manager through the coordinator, not through a hand
// call: a head transfer can leave the direct peers either side of this device
// unchanged, so a supporter would stay registered with a champion that no longer
// runs the duel. Drives the real HELLO path, so it fails if the manager stops
// subscribing to the coordinator's role edge.
inline void cdmHeadTransferResendsStandingConfirm(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    int confirmsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, _))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
            confirmsSent++;
            return 1;
        });

    // OUTPUT is a hunter's opponent jack, and a same-role peer there is what makes
    // this device a supporter rather than the champion.
    suite->connectOutputPort();
    uint8_t champion[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);
    ASSERT_TRUE(cdm.isSupporter());

    cdm.sendConfirm();
    ASSERT_EQ(confirmsSent, 1);

    // INPUT reaching CONNECTED demotes this device HEAD -> CHILD, and the opponent
    // jack is untouched, so the standing confirm has to survive the edge and go out
    // again. Wired through the coordinator, so it fails if the manager stops
    // subscribing to the role edge.
    suite->connectJackTo(suite->inJack, suite->supporterMac);

    EXPECT_EQ(confirmsSent, 2);
}

// A supporter that never pressed holds nothing to re-send. Without this the
// champion-changed trigger would register the whole chain as confirmed.
inline void cdmChampionChangeWithoutPressSendsNoConfirm(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _))
        .WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, _))
        .Times(0);

    uint8_t firstChampion[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, firstChampion, 1);
    uint8_t secondChampion[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, secondChampion, 2);
    cdm.resendConfirm();
}

// onRoleAnnounceReceived updates peerRoleByPort and championMac, acks sender,
// registers champion as ESP-NOW peer.
inline void cdmRoleAnnounceUpdatesChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t championMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    bool peerRegistered = false;
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
    EXPECT_TRUE(peerRegistered);
}

// Receiving announce with same championMac doesn't trigger cascade.
inline void cdmRoleAnnounceNoCascadeIfChampionUnchanged(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t championMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

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
    // Force championMac by receiving an announce.
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
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

// A supporter's screen shows the latest terminal result, so a newer WIN/LOSS must
// obsolete the previous one for the whole chain at once. Two live fan-outs would
// let the older one's retransmit land after the newer and flip the screen back —
// the hazard SendMode::SUPERSEDE_PER_TARGET exists to prevent for unicast.
inline void cdmNewTerminalEventSupersedesThePrevious(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _))
        .WillRepeatedly(Return(1));

    int eventFrames = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, _))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
            eventFrames++;
            return 1;
        });

    // Two terminal events inside one retry budget, neither acked.
    cdm.sendGameEventToSupporters(ChainGameEventType::WIN);
    cdm.sendGameEventToSupporters(ChainGameEventType::LOSS);
    const int afterSends = eventFrames;
    ASSERT_EQ(afterSends, 2);

    // One round must put ONE frame on the air: only the newer event is live.
    suite->fakeClock->advance(Resender::backoffMs(Resender::MAX_RETRIES) + 1);
    cdm.sync();
    EXPECT_EQ(eventFrames, afterSends + 1)
        << "the superseded event is still retransmitting alongside the current one";
}

// The opponent announce is the only thing that tells the other end of the duel
// cable what role we hold, and their canInitiateMatch refuses to start a match
// until they know it. Nothing else repairs a lost one — their announce to us
// populates our view of them, not theirs of us — so it has to retry.
inline void cdmOpponentAnnounceRetriesUntilDelivered(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    int announcesToOpponent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->opponentMac, 6) == 0) announcesToOpponent++;
            return 1;
        });

    suite->connectOutputPort();
    ASSERT_EQ(announcesToOpponent, 1) << "no announce when the duel cable came up";

    // The radio never reports it delivered, so it keeps trying.
    suite->fakeClock->advance(Resender::backoffMs(0) + 1);
    cdm.sync();
    EXPECT_GT(announcesToOpponent, 1)
        << "a lost role announce leaves the opponent unable to start a duel, forever";
}

// The same half-open gate as the supporter side, and it is reached by plugging
// the two cables in quick succession: the supporter jack coming up fires the
// cascade, which announces to BOTH jacks, while the opponent jack still holds a
// peer MAC off one inbound frame and nothing proving that peer can hear us.
inline void cdmOpponentAnnounceWaitsForConnected(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    int announcesToOpponent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->opponentMac, 6) == 0) announcesToOpponent++;
            return 1;
        });

    // Opponent cable in, its context not back yet.
    suite->beginConnectJackTo(suite->outJack, suite->opponentMac);
    ASSERT_NE(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    ASSERT_NE(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    // Supporter cable in. Its connect drives the cascade, which reaches the
    // opponent-jack announce while that link is still unproven.
    suite->connectInputPort();
    EXPECT_EQ(announcesToOpponent, 0) << "announced across a link that is not proven";

    // The opponent's context lands; now it can be told.
    suite->deliverPdnContext(suite->opponentMac);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
    EXPECT_GT(announcesToOpponent, 0) << "announce was suppressed rather than deferred";
}

// A jiggled cable is a link death and a reconnect to the same MAC. The champion
// must announce again: the supporter's own state went with the link, so it no
// longer knows who its champion is. Recording the announce against the peer MAC
// alone would match on the way back and skip it, orphaning that whole sub-chain.
inline void cdmReannouncesAfterSameMacReconnect(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).WillRepeatedly(Return(0));

    int announcesToSupporter = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) announcesToSupporter++;
            return 1;
        });

    // No manual cascade drives below this point: the manager is subscribed to
    // the coordinator, and whether the reconnect re-announces depends entirely
    // on which chain-change events the coordinator actually fires.
    suite->connectOutputPort();
    suite->connectInputPort();
    ASSERT_GT(announcesToSupporter, 0) << "no announce to begin with";
    const int afterFirst = announcesToSupporter;

    // Cable out: the HELLO heartbeat lapses and the link dies.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_NE(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::CONNECTED);

    // Same device back on the same jack.
    suite->connectInputPort();
    EXPECT_GT(announcesToSupporter, afterFirst)
        << "reconnected supporter was never told who its champion is";
}

// A champion change must reach the supporter jack even though the cable did not
// move. Gating the announce on the peer MAC alone suppresses exactly the case
// that matters: the head above us goes away, this device promotes itself, and
// the supporter below is never told — it keeps addressing joins and confirms to
// a device that is no longer champion, and drops this one's game events as
// coming from a stranger, so it contributes no boost for the rest of the round.
inline void cdmChampionChangeReachesAToldSupporter(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    uint8_t seqId = 0;
    std::array<uint8_t, 6> announcedChampion{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                RoleAnnouncePayload p;
                memcpy(&p, data, sizeof(p));
                seqId = p.seqId;
                memcpy(announcedChampion.data(), p.championMac, 6);
            }
            return 1;
        });

    // The supporter is told about champion C1, and the radio confirms it.
    uint8_t c1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, c1, 1);
    ASSERT_NE(seqId, 0u);
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, seqId, /*success=*/true);
    ASSERT_EQ(memcmp(announcedChampion.data(), c1, 6), 0);

    // The champion above changes. Same cable, same peer MAC, new fact.
    uint8_t c2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x77};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, c2, 2);

    EXPECT_EQ(memcmp(announcedChampion.data(), c2, 6), 0)
        << "the supporter was never told the new champion, so it still follows the old one";
}

// The supporter direction needs the same repair as the opponent one. A supporter
// that never learns its champion never joins the chain and never confirms, so it
// and everything below it contribute no boost for the whole round. The cascade
// would re-offer it — an undelivered announce leaves this peer reading as untold
// — but a settled chain raises no chain-state events at all, so the cascade never
// runs and the backstop is the only thing left.
inline void cdmUndeliveredSupporterAnnounceIsRetriedByBackstop(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).WillRepeatedly(Return(0));

    int announcesToSupporter = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) announcesToSupporter++;
            return 1;
        });

    suite->connectOutputPort();
    suite->connectInputPort();
    ASSERT_GT(announcesToSupporter, 0) << "no supporter announce to begin with";

    // The radio never confirms delivery, so the announce burns its whole budget
    // and is given up on. The supporter is still on the jack, still untold.
    for (int i = 0; i < Resender::MAX_RETRIES + 1; ++i) {
        suite->fakeClock->advance(Resender::backoffMs(Resender::MAX_RETRIES) + 1);
        cdm.sync();
    }
    const int afterAbandon = announcesToSupporter;

    // A full backstop interval later it is offered again.
    suite->fakeClock->advance(ChainDuelManager::ROLE_ANNOUNCE_BACKSTOP_MS + 1);
    cdm.sync();
    EXPECT_GT(announcesToSupporter, afterAbandon)
        << "supporter announce was abandoned and never re-offered; that supporter "
           "contributes no boost for the rest of the round";
}

// An opponent announce that exhausts its retry budget has to be offered again.
// Nothing else would: a settled chain raises no chain-state events, so the
// cascade never runs, and the opponent would never learn this device's role —
// its canInitiateMatch refuses that cable a duel for the rest of the round,
// recoverable only by re-seating it.
inline void cdmUndeliveredOpponentAnnounceIsRetriedByBackstop(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).WillRepeatedly(Return(0));

    int announcesToOpponent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->opponentMac, 6) == 0) announcesToOpponent++;
            return 1;
        });

    suite->connectOutputPort();
    suite->connectInputPort();
    ASSERT_GT(announcesToOpponent, 0) << "no opponent announce to begin with";

    // The radio never confirms delivery, so the announce burns its whole budget
    // and is given up on. This is the case the stamp must not record as told.
    for (int i = 0; i < Resender::MAX_RETRIES + 1; ++i) {
        suite->fakeClock->advance(Resender::backoffMs(Resender::MAX_RETRIES) + 1);
        cdm.sync();
    }
    const int afterAbandon = announcesToOpponent;

    // A full backstop interval later, the undelivered announce is re-offered.
    suite->fakeClock->advance(ChainDuelManager::ROLE_ANNOUNCE_BACKSTOP_MS + 1);
    cdm.sync();
    EXPECT_GT(announcesToOpponent, afterAbandon)
        << "an announce that was never delivered was recorded as delivered, so "
           "the opponent is never told this device's role";
}

// The announce must wait for a proven link. A supporter drops announces from a MAC
// it has not yet recorded as a direct peer, and the radio reports the frame sent
// anyway, so one offered at Connecting clears its own retry and is lost. Connected
// is the evidence the supporter already holds us. The second half is driven only
// by rdc.sync(), because the deferral is worthless unless the Connected
// transition itself re-fires the cascade.
inline void cdmAnnounceWaitsForConnectedSupporterJack(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    int announcesToSupporter = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) announcesToSupporter++;
            return 1;
        });

    // A champion: an opponent-jack peer, and nobody of our own role beyond it.
    suite->connectOutputPort();
    cdm.onChainStateChanged();

    // The supporter plugs in. Our jack holds its MAC off one HELLO, but its
    // context has not come back, so it has not necessarily recorded us.
    suite->beginConnectJackTo(suite->inJack, suite->supporterMac);
    ASSERT_NE(suite->rdc.getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);
    ASSERT_NE(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::CONNECTED);
    cdm.onChainStateChanged();
    EXPECT_EQ(announcesToSupporter, 0)
        << "announced to a supporter that cannot accept it yet";

    // Its context lands. The link is proven and the deferred announce goes.
    suite->deliverPdnContext(suite->supporterMac);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::CONNECTED);
    EXPECT_GT(announcesToSupporter, 0)
        << "announce was suppressed rather than deferred";
}

// Ack with matching seqId clears pending; subsequent sync does not retransmit.
inline void cdmAckClearsPending(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);

    // Capture the seqId from the supporter-jack send (that's what the role-announce entry tracks).
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

    // The radio reporting the frame delivered clears the retry; there is no
    // reply packet on this channel.
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, seqId, /*success=*/true);

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

    // A delivery report for a different destination clears nothing here: the
    // commit is matched on seqId AND MAC, so a stranger's report cannot mark
    // this peer as told.
    uint8_t otherMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00};
    suite->deliverRoleAnnounceSendResult(otherMac, seqId, /*success=*/true);

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

// Retransmit abandons after Resender::MAX_RETRIES with no ack.
inline void cdmRetransmitAbandonsAfterMax(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t champion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion, 1);

    int supporterSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            // Count sends to the supporter-jack peer (the one that goes into the role-announce entry).
            if (memcmp(mac, suite->supporterMac, 6) == 0) supporterSends++;
            return 1;
        });

    cdm.broadcastRoleAndChampion();  // one announce, to the supporter jack
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
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, seqId, /*success=*/true);

    auto s1 = cdm.getRetryStats();
    EXPECT_GE(s1.sends, 1u);
    EXPECT_EQ(s1.abandons, 0u);
    EXPECT_GE(s1.ackCount, 1u);
    EXPECT_GT(s1.ackLatencyMsSum, 0u);

    // A NEW champion is new content, so it goes out rather than being suppressed
    // as already-told. Nobody reports it delivered, so it exhausts its retries.
    uint8_t champion2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x77};
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, champion2, 2);
    // Tick at the cadence the platform loop actually uses. Sampling only once
    // per backstop interval would let the re-offer land in the same call as the
    // retry round and supersede the entry before its budget ever completes —
    // an artefact of the test's clock, not of the schedule.
    for (int i = 0; i < 19; i++) {
        suite->fakeClock->advance(100);
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

    // onChainStateChanged when still supporter: championMac remains unchanged.
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
    uint8_t seqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                firstCallSends++;
                RoleAnnouncePayload p;
                memcpy(&p, data, sizeof(p));
                seqId = p.seqId;
            }
            return 1;
        });
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    cdm.onChainStateChanged();
    ASSERT_TRUE(cdm.isChampion());
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(firstCallSends, 1);

    // The peer counts as told only once the radio confirms the frame reached it;
    // until then it is still owed an announce and the cascade is right to keep
    // offering one. Drive the delivery a real link would report.
    ASSERT_NE(seqId, 0u);
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, seqId, /*success=*/true);

    // Second call: supporter-jack peer unchanged and now told — must NOT
    // re-broadcast.
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

    // Leg 1: S1 (middle supporter) receives announce from A, updates championMac,
    // and cascades to its supporter-jack child.
    ChainDuelManager s1(&suite->player, suite->device.wirelessManager, &suite->rdc);
    s1.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);
    s1.setPeerRole(SerialIdentifier::INPUT_JACK, true);

    std::vector<std::array<uint8_t, 6>> s1Cascade;
    RoleAnnouncePayload s1CascadePayload{};
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

    // Leg 4: Champion A receives and records. The originator is two cables away,
    // so A places it from the join it sent, not from any jack of A's own.
    ChainDuelManager a(&suite->player, suite->device.wirelessManager, &suite->rdc);
    a.setPeerRole(SerialIdentifier::OUTPUT_JACK, false);
    a.setPeerRole(SerialIdentifier::INPUT_JACK, true);
    ASSERT_TRUE(a.isChampion());

    uint8_t distantMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    a.onChainJoinReceived(distantMac, suite->localMac);
    a.onConfirmReceived(distantMac, distantMac, confirmPayload.seqId);
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

// When a device was champion (championMac == self-MAC) and gains a same-role
// peer on its opponent jack (becoming a supporter), onChainStateChanged must
// clear championMac rather than cascade the stale self-MAC to its supporter.
inline void cdmChampionToSupporterClearsStaleSelfMac(ChainDuelManagerTests* suite) {
    suite->player.setIsHunter(true);
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // Allow all sends; connectOutputPort puts a context exchange on the air too.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    // Start as solo champion — self-assigns championMac to localMac.
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
// but does NOT update championMac. Prevents ping-pong between same-role peers.
inline void cdmRoleAnnounceFromSupporterJackIgnoresChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Seed championMac via opponent-jack announce first.
    uint8_t realChampion[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _)).WillRepeatedly(Return(1));
    cdm.onRoleAnnounceReceived(suite->opponentMac, 1, realChampion, 1);
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), realChampion, 6), 0);

    // Now a supporter-jack peer sends an announce with a different championMac
    // (simulating the ping-pong scenario). peerRoleByPort for supporter-jack
    // should update, but championMac should NOT change.
    uint8_t wrongChampion[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    cdm.onRoleAnnounceReceived(suite->supporterMac, 1, wrongChampion, 2);

    // championMac unchanged
    EXPECT_EQ(memcmp(cdm.getChampionMac(), realChampion, 6), 0);
}

// Role announce from opposite-role opponent-jack peer does NOT update championMac.
inline void cdmRoleAnnounceFromOppositeRoleOpponentIgnoresChampionMac(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);
    uint8_t selfMacArr[6];
    memcpy(selfMacArr, suite->localMac, 6);

    // Seed championMac to self via onChainStateChanged (champion self-assign).
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, _))
        .WillRepeatedly(Return(1));
    cdm.onChainStateChanged();
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), selfMacArr, 6), 0);

    // Receive announce from bounty (role=0) on opponent jack. Hunter champion
    // should ignore this — opposite-role sender is a duel opponent, not a chain parent.
    uint8_t bountyChampion[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    cdm.onRoleAnnounceReceived(suite->opponentMac, /*role=bounty*/0, bountyChampion, 7);

    // championMac must remain self (not overwritten with opponent's champion MAC).
    ASSERT_NE(cdm.getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(cdm.getChampionMac(), selfMacArr, 6), 0);
}

// After chain break (S1 disconnects from A), S1 becomes champion
// and updates championMac to self.
inline void chainDuelReconfigRecovers(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    uint8_t macA[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    // S1 starts with championMac=A (received from parent).
    ChainDuelManager s1(&suite->player, suite->device.wirelessManager, &suite->rdc);
    s1.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);
    s1.setPeerRole(SerialIdentifier::INPUT_JACK, true);

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
// The frame is broadcast and carries the champion's MAC, which is what tells a
// supporter several cables away that the result is from its own duel.
inline void cdmGameEventWinIsTrackedAndRetried(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    int winSends = 0;
    uint8_t winSeqId = 0;
    ChainGameEventPayload captured{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, MockDevice::BROADCAST_MAC, 6) == 0) {
                ChainGameEventPayload p;
                memcpy(&p, data, sizeof(p));
                if (p.event_type == (uint8_t)ChainGameEventType::WIN) {
                    winSeqId = p.seqId;
                    captured = p;
                    winSends++;
                }
            }
            return 1;
        });

    cdm.sendGameEventToSupporters(ChainGameEventType::WIN);
    EXPECT_EQ(winSends, 1);
    ASSERT_NE(winSeqId, 0u);
    EXPECT_EQ(memcmp(captured.championMac, suite->localMac, 6), 0);

    // Advance past first timeout (100ms), sync() should retransmit once.
    suite->fakeClock->advance(150);
    cdm.sync();
    EXPECT_EQ(winSends, 2);
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
            if (memcmp(mac, MockDevice::BROADCAST_MAC, 6) == 0) {
                ChainGameEventPayload p;
                memcpy(&p, data, sizeof(p));
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

// After Resender::MAX_RETRIES with no ACK, pending is abandoned — no further sends.
inline void cdmGameEventAbandonsAfterMax(ChainDuelManagerTests* suite) {
    suite->setupHunterChampion();
    ChainDuelManager cdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->applyHunterChampionRoles(cdm);

    int supporterSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, MockDevice::BROADCAST_MAC, 6) == 0) supporterSends++;
            return 1;
        });

    cdm.sendGameEventToSupporters(ChainGameEventType::WIN);
    ASSERT_EQ(supporterSends, 1);

    // Resender::MAX_RETRIES = 3. Advance past each exponential backoff window (100, 200,
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

