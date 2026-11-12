#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "rdc-hello-tests.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/posse-manager.hpp"
#include "game/player.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// Physical connectivity here is the production HELLO path: framed HELLO bytes
// pushed into a native jack driver, drained by its exec() pump into the RDC's
// parser, then the ESP-NOW context exchange completed to commit the link.
class PosseManagerTests : public testing::Test {
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
        .WillByDefault(
        testing::DoAll(testing::SaveArg<1>(&contextHandler), testing::SaveArg<2>(&contextCtx)));
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

    /**
     * Feeds a peer's PdnConnectionContext in through the handler the reliable
     * transport registered with the radio driver — the production receive path.
     */
    void deliverPdnContext(const uint8_t* peerMac, bool isHunter = false) {
        if (contextHandler == nullptr) return;
        std::vector<uint8_t> bytes =
        pdnContextBytes(ChainRole::STANDALONE, /*userId=*/4242, ++contextSeqId, isHunter ? 1 : 0);
        contextHandler(peerMac, bytes.data(), bytes.size(), contextCtx);
    }

    void connectJackTo(NativeSerialDriver& jack, const uint8_t* peerMac,
                       const uint8_t* advertisedHead = nullptr, bool isHunter = false) {
        deliverFrame(jack, chainHelloFrame(peerMac, advertisedHead));
        rdc.sync(&device);
        deliverPdnContext(peerMac, isHunter);
        rdc.sync(&device);
    }

    /**
     * Only the peer's first HELLO: the jack holds its MAC but the exchange has
     * not completed, so it stays CONNECTING.
     */
    void beginConnectJackTo(NativeSerialDriver& jack, const uint8_t* peerMac) {
        deliverFrame(jack, chainHelloFrame(peerMac, nullptr));
        rdc.sync(&device);
    }

    /** Brings the supporter-side jack up against supporterMac. */
    void connectInputJack(bool isHunter = true) {
        connectJackTo(inJack, supporterMac, nullptr, isHunter);
    }

    /** Brings the opponent-side jack up against opponentMac. */
    void connectOutputJack(bool isHunter = false) {
        connectJackTo(outJack, opponentMac, nullptr, isHunter);
    }

    /** Configures this fixture device as a Hunter Challenger connected to an opponent and
     * supporter. */
    void setupHunterChallenger() {
        player.setIsHunter(true);
        connectOutputJack(false);
        connectInputJack(true);
    }

    /**
     * Closes a ring around this device: it heads a chain out of OUTPUT and its
     * own MAC comes back on INPUT, which is the only local evidence of closure.
     */
    void closeRingAroundSelf() {
        connectOutputJack();
        connectJackTo(inJack, supporterMac, localMac, true);
    }

    MockDevice device;
    NativeSerialDriver outJack{"posse-out"};
    NativeSerialDriver inJack{"posse-in"};
    // Declared after the jacks so it is destroyed first; its dtor clears their
    // byte callbacks, mirroring production where the drivers outlive the RDC.
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    Player player;

    /**
     * Replays a radio send result for a role announce, the way the driver
     * would after the frame goes out. success=false is SEND_FAIL.
     */
    void deliverRoleAnnounceSendResult(const uint8_t* toMac, uint8_t seqId, bool success) {
        if (roleAnnounceSendStatus == nullptr) return;
        RoleAnnouncePayload echoed{};
        echoed.seqId = seqId;
        roleAnnounceSendStatus(toMac, reinterpret_cast<const uint8_t*>(&echoed), sizeof(echoed),
                               success, roleAnnounceSendStatusCtx);
    }

    /** One device tick as the posse sees it: the shared transport, then the manager. */
    void tick(PosseManager& posse) {
        device.wirelessManager->sync();
        posse.sync();
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

/**
 * Runs the main loop for `ms`, one sync() every 20ms — fine enough that no 100ms
 * backoff window is skipped, not a claim about the device's loop period.
 */
inline void runLoopFor(PosseManagerTests* suite, PosseManager& posse, unsigned long ms) {
    for (unsigned long elapsed = 0; elapsed < ms; elapsed += 20) {
        suite->fakeClock->advance(20);
        posse.sync();
    }
}

// Role derivation: challenger topology produces correct is* / canInitiate / peers results.
// Without peers everything returns false/empty; with a full challenger setup everything flips.
inline void posseRoleDerivationWithChallengerTopology(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // No peers: default-challenger (isSupporter is false, no opponent peer).
    EXPECT_TRUE(posse.isChallenger());
    EXPECT_FALSE(posse.isSupporter());
    EXPECT_FALSE(posse.canInitiateMatch());
    EXPECT_TRUE(posse.getSupporterChainPeers().empty());

    suite->setupHunterChallenger();
    PosseManager posse2(&suite->player, suite->device.wirelessManager, &suite->rdc);

    EXPECT_TRUE(posse2.isChallenger());
    EXPECT_FALSE(posse2.isSupporter());
    EXPECT_TRUE(posse2.canInitiateMatch());
    EXPECT_FALSE(posse2.getSupporterChainPeers().empty());
}

// Half-open gate (#162): a jack that only reached CONNECTING knows its peer MAC
// from one serial frame but has no proven return path, and a match pushed across
// it strands the initiator waiting for an ack that never comes. Only a CONNECTED
// opponent jack opens the gate.
inline void posseCanInitiateMatchRequiresConnectedOpponentJack(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // One HELLO: the OUTPUT jack knows the peer MAC, but the context exchange
    // has not answered, so the connection is still half-open.
    suite->beginConnectJackTo(suite->outJack, suite->opponentMac);

    ASSERT_EQ(suite->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTING);
    EXPECT_FALSE(posse.canInitiateMatch());

    // The peer's context lands, proving a path back.
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/false);
    suite->rdc.sync(&suite->device);

    ASSERT_EQ(suite->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTED);
    ASSERT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::PDN);
    EXPECT_TRUE(posse.canInitiateMatch());
}

// Bounties never initiate matches regardless of topology
inline void posseCanInitiateMatchFalseForBounty(PosseManagerTests* suite) {
    suite->player.setIsHunter(false);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_FALSE(posse.canInitiateMatch());
}

// Confirm lifecycle: starts at 0, increments per unique MAC, dedup blocks repeats, clear resets
inline void posseConfirmLifecycle(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    ASSERT_TRUE(posse.isChallenger());

    EXPECT_EQ(posse.getBoostMs(), 0u);

    posse.onConfirmReceived(suite->supporterMac);
    EXPECT_EQ(posse.getBoostMs(), 15u);
    EXPECT_EQ(posse.getConfirmedSupporterCount(), 1u);

    posse.onConfirmReceived(suite->supporterMac);
    EXPECT_EQ(posse.getConfirmedSupporterCount(), 1u);

    posse.clearSupporterConfirms();
    EXPECT_EQ(posse.getBoostMs(), 0u);
    EXPECT_EQ(posse.getConfirmedSupporterCount(), 0u);
}

// 10. onChainStateChanged clears confirms when chain drains to 0
inline void posseOnChainStateChangedClearsOnDrain(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    ASSERT_TRUE(posse.isChallenger());

    posse.onConfirmReceived(suite->supporterMac);
    ASSERT_EQ(posse.getBoostMs(), 15u);

    // First call: chain has peers (lastSupporterChainCount goes from 0 to N)
    posse.onChainStateChanged();
    EXPECT_EQ(posse.getBoostMs(), 15u);

    // Disconnect the input jack by advancing time past heartbeat timeout
    suite->fakeClock->advance(5000);
    suite->rdc.sync(&suite->device);

    // Second call: chain drained to 0
    posse.onChainStateChanged();
    EXPECT_EQ(posse.getBoostMs(), 0u);
    EXPECT_EQ(posse.getConfirmedSupporterCount(), 0u);
}

// Confirm with originator MAC not present in the challenger's supporter-jack
// chain must be rejected. Covers MAC-spoof injection by an in-range stranger.
inline void posseConfirmFromUnknownOriginatorRejected(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    ASSERT_TRUE(posse.isChallenger());

    uint8_t stranger[6] = {0xBA, 0xAD, 0xF0, 0x0D, 0x00, 0x00};
    posse.onConfirmReceived(stranger);
    EXPECT_EQ(posse.getBoostMs(), 0u);
    EXPECT_EQ(posse.getConfirmedSupporterCount(), 0u);
}

// Middle-of-chain device (same-role peer on opponent jack) is NOT challenger.
inline void posseIsChallengerFalseWithSameRoleOpponent(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    EXPECT_TRUE(posse.isSupporter());
    EXPECT_FALSE(posse.isChallenger());
}

// Inside a closed ring nobody is a challenger: the shootout owns that topology.
inline void posseIsChallengerFalseInRing(PosseManagerTests* suite) {
    suite->closeRingAroundSelf();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    ASSERT_TRUE(suite->rdc.isInRing());
    EXPECT_FALSE(posse.isChallenger());
}

// sendConfirm targets challengerMac directly with a ChainConfirmPayload.
inline void posseSendConfirmTargetsChallengerMac(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    // Seed challengerMac via announce.
    uint8_t challenger[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);

    std::array<uint8_t, 6> target{};
    ChainConfirmPayload captured{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
    .WillOnce([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
        memcpy(target.data(), mac, 6);
        memcpy(&captured, data, sizeof(captured));
        return 1;
    });

    posse.sendConfirm();

    EXPECT_EQ(memcmp(target.data(), challenger, 6), 0);
    EXPECT_EQ(memcmp(captured.originatorMac, suite->localMac, 6), 0);
}

// sendConfirm increments seqId on each call.
inline void posseSendConfirmIncrementsSeqId(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    uint8_t challenger[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);

    std::vector<uint8_t> seqIds;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t* data, const size_t) {
        ChainConfirmPayload p;
        memcpy(&p, data, sizeof(p));
        seqIds.push_back(p.seqId);
        return 1;
    });

    posse.sendConfirm();
    posse.sendConfirm();
    posse.sendConfirm();
    ASSERT_EQ(seqIds.size(), 3u);
    ASSERT_NE(seqIds[0], 0u);
    const uint8_t expected1 = seqIds[0] == 255 ? 1 : seqIds[0] + 1;
    const uint8_t expected2 = expected1 == 255 ? 1 : expected1 + 1;
    EXPECT_EQ(seqIds[1], expected1);
    EXPECT_EQ(seqIds[2], expected2);
}

// sendConfirm is a noop when challengerMac is not set.
inline void posseSendConfirmNoopWhenChallengerMacInvalid(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // No announce received; challengerMac is nullopt.

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainConfirm, _, _)).Times(0);

    posse.sendConfirm();
}

// A confirm can outrun the join that puts its originator in the challenger's
// supporter chain. Held rather than dropped, and admitted the moment the join
// lands — otherwise that supporter contributes nothing all round.
inline void posseConfirmBufferedUntilOriginatorJoinsChain(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    ASSERT_TRUE(posse.isChallenger());

    uint8_t multiHopMac[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    posse.onConfirmReceived(multiHopMac);
    ASSERT_EQ(posse.getConfirmedSupporterCount(), 0u);

    // The join lands: multiHopMac sits somewhere behind the direct supporter-jack
    // peer and has just named this device as its challenger.
    posse.onChainJoinReceived(multiHopMac, suite->localMac);

    posse.onChainStateChanged();

    EXPECT_EQ(posse.getConfirmedSupporterCount(), 1u);
    EXPECT_EQ(posse.getBoostMs(), PosseManager::BOOST_PER_SUPPORTER_MS);
}

// Saturating the confirm slots with strangers must not silence a real supporter.
// The slots are fixed and the packet handler cannot prove an originator is a
// member, so anything on the channel can fill them; refusing new entries when
// full, or holding rejected entries forever, would turn that into a mute button
// for the whole round.
inline void posseStrangerConfirmsCannotSilenceRealSupporter(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    ASSERT_TRUE(posse.isChallenger());

    // Twice the slot count, none of them ever in the roster.
    for (int i = 0; i < 36; i++) {
        uint8_t stranger[6] = {
        0xEE, 0xEE, 0xEE, 0xEE, static_cast<uint8_t>(i), static_cast<uint8_t>(i)};
        posse.onConfirmReceived(stranger);
    }
    ASSERT_EQ(posse.getConfirmedSupporterCount(), 0u);

    // A genuine multi-hop supporter presses afterwards and joins the roster.
    uint8_t realMac[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    posse.onConfirmReceived(realMac);
    posse.onChainJoinReceived(realMac, suite->localMac);
    posse.onChainStateChanged();

    EXPECT_EQ(posse.getConfirmedSupporterCount(), 1u);
    EXPECT_EQ(posse.getBoostMs(), PosseManager::BOOST_PER_SUPPORTER_MS);
}

// A join is a MAC claiming chain membership, so it has to name the challenger it
// claims to follow. Chains run side by side in one room and every frame is in
// range of all of them; without the check, one chain's supporter would enrol
// itself in another's roster and buy that challenger boost it never earned.
inline void posseChainJoinForAnotherChallengerIsIgnored(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    ASSERT_TRUE(posse.isChallenger());

    uint8_t otherChallenger[6] = {0x99, 0x99, 0x99, 0x99, 0x99, 0x99};
    uint8_t foreignSupporter[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    posse.onChainJoinReceived(foreignSupporter, otherChallenger);
    posse.onConfirmReceived(foreignSupporter);

    EXPECT_EQ(posse.getConfirmedSupporterCount(), 0u);
    EXPECT_EQ(posse.getBoostMs(), 0u);

    // The same device, now naming us, is admitted — the MAC was never the issue.
    posse.onChainJoinReceived(foreignSupporter, suite->localMac);
    EXPECT_EQ(posse.getConfirmedSupporterCount(), 1u);
}

// Head transfer swaps the challenger under a supporter that already pressed. The
// standing confirm has to follow it or the press buys no boost.
inline void posseConfirmResentWhenChallengerChanges(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));

    std::vector<std::array<uint8_t, 6>> confirmTargets;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
        std::array<uint8_t, 6> target;
        memcpy(target.data(), mac, 6);
        confirmTargets.push_back(target);
        return 1;
    });

    uint8_t firstChallenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    posse.onRoleAnnounceReceived(suite->opponentMac, firstChallenger);
    ASSERT_TRUE(posse.isSupporter());
    posse.sendConfirm();

    uint8_t secondChallenger[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    posse.onRoleAnnounceReceived(suite->opponentMac, secondChallenger);

    ASSERT_EQ(confirmTargets.size(), 2u);
    EXPECT_EQ(memcmp(confirmTargets[0].data(), firstChallenger, 6), 0);
    EXPECT_EQ(memcmp(confirmTargets[1].data(), secondChallenger, 6), 0);
}

// COUNTDOWN wipes the challenger's roll call, so the press that preceded it is
// spent. A challenger change after one must not resurrect it.
inline void posseCountdownVoidsStandingConfirm(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));

    int confirmsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainConfirm, _, _))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
        confirmsSent++;
        return 1;
    });

    uint8_t firstChallenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    posse.onRoleAnnounceReceived(suite->opponentMac, firstChallenger);
    ASSERT_TRUE(posse.isSupporter());
    posse.sendConfirm();
    ASSERT_EQ(confirmsSent, 1);

    posse.onChainGameEventReceived(static_cast<uint8_t>(ChainGameEventType::COUNTDOWN));
    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    posse.sync();

    uint8_t secondChallenger[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    posse.onRoleAnnounceReceived(suite->opponentMac, secondChallenger);

    EXPECT_EQ(confirmsSent, 1);
}

// Unplugging ends the round for this supporter. The press it made must not
// follow it into whatever chain it is patched into next.
inline void posseSupporterRoleLossVoidsStandingConfirm(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Tearing the links down makes the RDC emit its own traffic; a catch-all
    // fallback keeps that off the confirm counter below.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    int joinsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainJoin, _, _))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
        joinsSent++;
        return 1;
    });

    int confirmsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainConfirm, _, _))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
        confirmsSent++;
        return 1;
    });

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS - 10);
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    uint8_t challenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);
    ASSERT_TRUE(posse.isSupporter());
    posse.sendConfirm();
    ASSERT_EQ(joinsSent, 1);
    ASSERT_EQ(confirmsSent, 1);

    // Cable out on both jacks: the heartbeat lapses and the direct peers go.
    suite->fakeClock->advance(11);
    suite->rdc.sync(&suite->device);
    posse.onChainStateChanged();
    ASSERT_FALSE(posse.isSupporter());

    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    posse.sync();
    posse.resendConfirm();
    EXPECT_EQ(joinsSent, 1);
    EXPECT_EQ(confirmsSent, 1);
}

// The role edge reaches the manager through the coordinator, not through a hand
// call: a head transfer can leave the direct peers either side of this device
// unchanged, so a supporter would stay registered with a challenger that no longer
// runs the match. Drives the real HELLO path, so it fails if the manager stops
// subscribing to the coordinator's role edge.
inline void posseHeadTransferResendsStandingConfirm(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    int confirmsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainConfirm, _, _))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
        confirmsSent++;
        return 1;
    });

    // OUTPUT is a hunter's opponent jack, and a same-role peer there is what makes
    // this device a supporter rather than the challenger.
    suite->connectOutputJack(/*isHunter=*/true);
    uint8_t challenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);
    ASSERT_TRUE(posse.isSupporter());

    posse.sendConfirm();
    ASSERT_EQ(confirmsSent, 1);

    // INPUT reaching CONNECTED demotes this device HEAD -> CHILD, and the opponent
    // jack is untouched, so the standing confirm has to survive the edge and go out
    // again. Wired through the coordinator, so it fails if the manager stops
    // subscribing to the role edge.
    suite->connectJackTo(suite->inJack, suite->supporterMac, nullptr, /*isHunter=*/true);

    EXPECT_EQ(confirmsSent, 2);
}

// A supporter that never pressed holds nothing to re-send. Without this the
// challenger-changed trigger would register the whole chain as confirmed.
inline void posseChallengerChangeWithoutPressSendsNoConfirm(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainConfirm, _, _)).Times(0);

    uint8_t firstChallenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    posse.onRoleAnnounceReceived(suite->opponentMac, firstChallenger);
    uint8_t secondChallenger[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    posse.onRoleAnnounceReceived(suite->opponentMac, secondChallenger);
    posse.resendConfirm();
}

inline void posseRoleAnnounceUpdatesChallengerMac(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t challengerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));

    posse.onRoleAnnounceReceived(suite->opponentMac, challengerMac);

    ASSERT_NE(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(posse.getChallengerMac(), challengerMac, 6), 0);
}

inline void posseRingClosureClearsTheChallenger(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    uint8_t challengerMac[6] = {0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F};

    uint8_t initialSeqId = 0;
    int supporterAnnounces = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) {
            RoleAnnouncePayload p;
            memcpy(&p, data, sizeof(p));
            initialSeqId = p.seqId;
            supporterAnnounces++;
        }
        return 1;
    });

    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    posse.onChainStateChanged();
    ASSERT_NE(initialSeqId, 0u);
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, initialSeqId, /*success=*/true);

    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    posse.onRoleAnnounceReceived(suite->opponentMac, challengerMac);
    ASSERT_NE(posse.getChallengerMac(), nullptr);
    posse.sendConfirm();

    deliverFrame(suite->inJack,
                 chainHelloFrame(suite->supporterMac, challengerMac, HELLO_FLAG_RING_CLOSED));
    suite->rdc.sync(&suite->device);
    posse.sync();

    EXPECT_EQ(posse.getChallengerMac(), nullptr) << "a ring still names a challenger";

    int ringSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainConfirm, _, _))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
        ringSends++;
        return 1;
    });
    const int announcesBeforeRingRx = supporterAnnounces;
    posse.onRoleAnnounceReceived(suite->opponentMac, challengerMac);
    suite->fakeClock->advance(Resender::backoffMs(0) + 1);
    posse.sync();
    EXPECT_EQ(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(ringSends, 0);
    EXPECT_EQ(supporterAnnounces, announcesBeforeRingRx);

    deliverFrame(suite->outJack, chainHelloFrame(suite->opponentMac, nullptr));
    deliverFrame(suite->inJack, chainHelloFrame(suite->supporterMac, challengerMac, 0));
    suite->rdc.sync(&suite->device);
    posse.sync();
    EXPECT_EQ(posse.getChallengerMac(), nullptr);

    posse.onRoleAnnounceReceived(suite->opponentMac, challengerMac);
    ASSERT_NE(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(posse.getChallengerMac(), challengerMac, 6), 0);
    EXPECT_GT(supporterAnnounces, announcesBeforeRingRx);
}

// An announce from a device that has no challenger of its own leaves the field
// zeroed. Caching that address would cascade it onward as this device's challenger.
inline void posseRoleAnnounceWithNoChallengerIsRefused(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t challengerMac[6] = {0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F};
    uint8_t zeroMac[6] = {0, 0, 0, 0, 0, 0};

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));

    posse.onRoleAnnounceReceived(suite->opponentMac, challengerMac);
    ASSERT_NE(posse.getChallengerMac(), nullptr);

    posse.onRoleAnnounceReceived(suite->opponentMac, zeroMac);

    EXPECT_EQ(posse.getChallengerMac(), nullptr) << "an all-zero challenger was cached";
}

// Receiving announce with same challengerMac doesn't trigger cascade.
inline void posseRoleAnnounceNoCascadeIfChallengerUnchanged(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t challengerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // First receive triggers a cascade broadcast (challengerMac is new); may send to
    // both jacks so we allow any number >= 1.
    int firstCascadeCount = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
        firstCascadeCount++;
        return 1;
    });
    posse.onRoleAnnounceReceived(suite->opponentMac, challengerMac);
    EXPECT_GE(firstCascadeCount, 1);

    // Second receive with same challengerMac — assert no kRoleAnnounce emit (cascade).
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _)).Times(0);

    posse.onRoleAnnounceReceived(suite->opponentMac, challengerMac);
}

inline void posseBroadcastRoleAndChallengerSends(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Force challengerMac by receiving an announce.
    uint8_t challenger[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);
    // broadcastRoleAndChallenger was already called internally by onRoleAnnounceReceived.
    // Here we just verify the last broadcast had correct content by calling again.

    bool sentToSupporter = false;
    RoleAnnouncePayload capturedFromSupporter{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) {
            sentToSupporter = true;
            memcpy(&capturedFromSupporter, data, sizeof(capturedFromSupporter));
        }
        return 1;
    });

    posse.broadcastRoleAndChallenger();

    // supporter jack for hunter = INPUT_JACK; setupHunterChallenger puts
    // supporterMac on INPUT.
    EXPECT_TRUE(sentToSupporter);
    EXPECT_EQ(memcmp(capturedFromSupporter.challengerMac, challenger, 6), 0);
}

// A supporter's screen shows the latest terminal result, so a newer WIN/LOSS must
// obsolete the previous one for the whole chain at once. Two live fan-outs would
// let the older one's retransmit land after the newer and flip the screen back —
// the hazard SendMode::SUPERSEDE_PER_TARGET exists to prevent for unicast.
inline void posseNewTerminalEventSupersedesThePrevious(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));

    int eventFrames = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainGameEvent, _, _))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t*, const size_t) {
        eventFrames++;
        return 1;
    });

    // Two terminal events inside one frame's span, neither acked.
    posse.sendGameEventToSupporters(ChainGameEventType::WIN);
    posse.sendGameEventToSupporters(ChainGameEventType::LOSS);
    const int afterSends = eventFrames;
    ASSERT_EQ(afterSends, 2);

    // One round must put ONE frame on the air: only the newer event is live.
    suite->fakeClock->advance(Resender::backoffMs(0) + 1);
    posse.sync();
    EXPECT_EQ(eventFrames, afterSends + 1)
    << "the superseded event is still retransmitting alongside the current one";
}

// A jiggled cable is a link death and a reconnect to the same MAC. The challenger
// must announce again: the supporter's own state went with the link, so it no
// longer knows who its challenger is. Recording the announce against the peer MAC
// alone would match on the way back and skip it, orphaning that whole sub-chain.
inline void posseReannouncesAfterSameMacReconnect(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

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
    suite->connectOutputJack();
    suite->connectInputJack();
    ASSERT_GT(announcesToSupporter, 0) << "no announce to begin with";
    const int afterFirst = announcesToSupporter;

    // Cable out: the HELLO heartbeat lapses and the link dies.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_NE(suite->rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::CONNECTED);

    // Same device back on the same jack.
    suite->connectInputJack();
    EXPECT_GT(announcesToSupporter, afterFirst)
    << "reconnected supporter was never told who its challenger is";
}

// A challenger change must reach the supporter jack even though the cable did not
// move. Gating the announce on the peer MAC alone suppresses exactly the case
// that matters: the head above us goes away, this device promotes itself, and
// the supporter below is never told — it keeps addressing joins and confirms to
// a device that is no longer challenger, and drops this one's game events as
// coming from a stranger, so it contributes no boost for the rest of the round.
inline void posseChallengerChangeReachesAToldSupporter(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    uint8_t seqId = 0;
    std::array<uint8_t, 6> announcedChallenger{};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) {
            RoleAnnouncePayload p;
            memcpy(&p, data, sizeof(p));
            seqId = p.seqId;
            memcpy(announcedChallenger.data(), p.challengerMac, 6);
        }
        return 1;
    });

    // The supporter is told about challenger C1, and the radio confirms it.
    uint8_t c1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    posse.onRoleAnnounceReceived(suite->opponentMac, c1);
    ASSERT_NE(seqId, 0u);
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, seqId, /*success=*/true);
    ASSERT_EQ(memcmp(announcedChallenger.data(), c1, 6), 0);

    // The challenger above changes. Same cable, same peer MAC, new fact.
    uint8_t c2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x77};
    posse.onRoleAnnounceReceived(suite->opponentMac, c2);

    EXPECT_EQ(memcmp(announcedChallenger.data(), c2, 6), 0)
    << "the supporter was never told the new challenger, so it still follows the old one";
}

inline void posseUndeliveredSupporterAnnounceIsRetriedByBackstop(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    int announcesToSupporter = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) announcesToSupporter++;
        return 1;
    });

    suite->connectOutputJack();
    suite->connectInputJack();
    ASSERT_GT(announcesToSupporter, 0) << "no supporter announce to begin with";

    // The radio never confirms delivery, so the announce runs out of span and is
    // given up on. The supporter is still on the jack, still untold.
    runLoopFor(suite, posse, Resender::RETRANSMIT_SPAN_MS + 100);
    const int afterAbandon = announcesToSupporter;

    // A full backstop interval later it is offered again.
    suite->fakeClock->advance(PosseManager::ROLE_ANNOUNCE_BACKSTOP_MS + 1);
    posse.sync();
    EXPECT_GT(announcesToSupporter, afterAbandon)
    << "supporter announce was abandoned and never re-offered; that supporter "
       "contributes no boost for the rest of the round";
}

// The announce must wait for a proven link. A supporter drops announces from a MAC
// it has not yet recorded as a direct peer, and the radio reports the frame sent
// anyway, so one offered at Connecting clears its own retry and is lost. Connected
// is the evidence the supporter already holds us. The second half is driven only
// by rdc.sync(), because the deferral is worthless unless the Connected
// transition itself re-fires the cascade.
inline void posseAnnounceWaitsForConnectedSupporterJack(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    int announcesToSupporter = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) announcesToSupporter++;
        return 1;
    });

    // A challenger: an opponent-jack peer, and nobody of our own role beyond it.
    suite->connectOutputJack();
    posse.onChainStateChanged();

    // The supporter plugs in. Our jack holds its MAC off one HELLO, but its
    // context has not come back, so it has not necessarily recorded us.
    suite->beginConnectJackTo(suite->inJack, suite->supporterMac);
    ASSERT_NE(suite->rdc.getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);
    ASSERT_NE(suite->rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::CONNECTED);
    posse.onChainStateChanged();
    EXPECT_EQ(announcesToSupporter, 0) << "announced to a supporter that cannot accept it yet";

    // Its context lands. The link is proven and the deferred announce goes.
    suite->deliverPdnContext(suite->supporterMac, /*isHunter=*/true);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::CONNECTED);
    EXPECT_GT(announcesToSupporter, 0) << "announce was suppressed rather than deferred";
}

// Ack with matching seqId clears pending; subsequent sync does not retransmit.
inline void posseAckClearsPending(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t challenger[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);

    // Capture the seqId from the supporter-jack send (that's what the role-announce entry tracks).
    uint8_t seqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) {
            RoleAnnouncePayload p;
            memcpy(&p, data, sizeof(p));
            seqId = p.seqId;
        }
        return 1;
    });
    posse.broadcastRoleAndChallenger();
    ASSERT_NE(seqId, 0u);

    // The radio reporting the frame delivered clears the retry; there is no
    // reply packet on this channel.
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, seqId, /*success=*/true);

    // Subsequent sync() after timeout must not emit any further role announces.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _)).Times(0);
    suite->fakeClock->advance(150);
    posse.sync();
}

// Ack with matching seqId but wrong fromMac must not clear pending.
inline void posseAckFromWrongMacIgnored(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t challenger[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);

    uint8_t seqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) {
            RoleAnnouncePayload p;
            memcpy(&p, data, sizeof(p));
            seqId = p.seqId;
        }
        return 1;
    });
    posse.broadcastRoleAndChallenger();
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
    suite->tick(posse);
    EXPECT_EQ(retransmits, 1);
}

// Retransmit abandons once the span runs out with no ack.
inline void posseRetransmitAbandonsWhenSpanRunsOut(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t challenger[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    // Allow implicit broadcast triggered inside onRoleAnnounceReceived.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);

    int supporterSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
        // Count sends to the supporter-jack peer (the one that goes into the role-announce entry).
        if (memcmp(mac, suite->supporterMac, 6) == 0) supporterSends++;
        return 1;
    });

    posse.broadcastRoleAndChallenger();  // one announce, to the supporter jack
    ASSERT_EQ(supporterSends, 1);

    // Advance clock + sync 3 times → 3 retransmits (supporter only, pending path).
    // Gaps of 100, 200, 400ms, so one jump past the biggest fires each round. 450
    // rather than 500: three 500ms jumps put the last retransmit on the same
    // millisecond the span ends, leaving the case to turn on expired() being strict.
    // At 450 the third round has 150ms of room ahead of the deadline.
    for (int i = 0; i < 3; i++) {
        suite->fakeClock->advance(450);
        suite->tick(posse);
    }
    EXPECT_EQ(supporterSends, 4);  // 1 initial + 3 retransmits

    // 4th sync should not emit — pending abandoned.
    suite->fakeClock->advance(500);
    suite->tick(posse);
    EXPECT_EQ(supporterSends, 4);
}

// A role announce is acked on SEND_SUCCESS and timed; one nobody confirms is
// retried inside its span and then given up on.
inline void posseRoleAnnounceTimesDeliveryAndRetransmits(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t challenger[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    uint8_t seqId = 0;
    int announceFrames = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
        if (memcmp(mac, suite->supporterMac, 6) == 0) {
            RoleAnnouncePayload p;
            memcpy(&p, data, sizeof(p));
            seqId = p.seqId;
            announceFrames++;
        }
        return 1;
    });

    // Initial announce triggers one send, then an ack clears it.
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);
    ASSERT_NE(seqId, 0u);
    suite->fakeClock->advance(50);
    suite->deliverRoleAnnounceSendResult(suite->supporterMac, seqId, /*success=*/true);

    PosseManager::RetryStats s1 = posse.getRetryStats();
    EXPECT_GE(s1.ackCount, 1u);
    EXPECT_GT(s1.ackLatencyMsSum, 0u);

    // A NEW challenger is new content, so it goes out rather than being suppressed
    // as already-told. Nobody reports it delivered, so it exhausts its retries.
    uint8_t challenger2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x77};
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger2);
    // Tick at the cadence the platform loop actually uses. Sampling only once
    // per backstop interval would let the re-offer land in the same call as the
    // retry round and supersede the entry before its span ever runs out —
    // an artefact of the test's clock, not of the schedule.
    for (int i = 0; i < 19; i++) {
        suite->fakeClock->advance(100);
        suite->tick(posse);
    }
    // The acked announce, then the unconfirmed one and its three retransmits
    // inside the span.
    EXPECT_EQ(announceFrames, 5);
}

// Becoming challenger self-assigns challengerMac to own MAC.
inline void posseOnChainStateBecomesChallengerSetsSelfMac(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Solo device — isChallenger() is true by default under current semantics.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));

    posse.onChainStateChanged();

    ASSERT_NE(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(posse.getChallengerMac(), suite->localMac, 6), 0);
}

// A supporter that inherited challengerMac from an upstream announce keeps
// that cached value across onChainStateChanged; only a self-MAC value is
// cleared (see posseChallengerToSupporterClearsStaleSelfMac).
inline void posseSupporterKeepsUpstreamChallengerMacAfterTransition(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    uint8_t challenger[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    posse.onRoleAnnounceReceived(suite->opponentMac, challenger);
    ASSERT_NE(posse.getChallengerMac(), nullptr);
    ASSERT_TRUE(posse.isSupporter());
    ASSERT_FALSE(posse.isChallenger());

    // onChainStateChanged when still supporter: challengerMac remains unchanged.
    posse.onChainStateChanged();
    EXPECT_NE(posse.getChallengerMac(), nullptr);
}

// onChainStateChanged fires broadcast ONCE when a new supporter-jack peer appears,
// and does NOT re-fire on subsequent calls with the same peer.
inline void posseOnChainStateNewSupporterTriggersBroadcast(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // First call: device is challenger (self-assign), supporter-jack peer present →
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
    posse.onChainStateChanged();
    ASSERT_TRUE(posse.isChallenger());
    ASSERT_NE(posse.getChallengerMac(), nullptr);
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
    posse.onChainStateChanged();
    EXPECT_EQ(secondCallSends, 0);
}

// End-to-end: Challenger A receives a direct confirm from the originator S2
// after the join-announce cascade has propagated challengerMac=A to S2.
// Drives S1 (the middle supporter) to cascade the announce, and S2 to
// target A directly.
inline void posseThreeDeviceConfirm(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    uint8_t macA[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    // One manager at a time: the device has one transport, and a PktType is
    // claimed by one channel on it. The mock actions outlive the blocks, so
    // what they capture lives here.
    std::vector<std::array<uint8_t, 6>> s1Cascade;
    RoleAnnouncePayload s1CascadePayload{};
    std::array<uint8_t, 6> confirmTarget{};
    ChainConfirmPayload confirmPayload{};

    // Leg 1: S1 (middle supporter) receives announce from A, updates challengerMac,
    // and cascades to its supporter-jack child.
    {
        PosseManager s1(&suite->player, suite->device.wirelessManager, &suite->rdc);

        EXPECT_CALL(*suite->device.mockPeerComms,
                    sendData(_, PktType::kRoleAnnounce, _, sizeof(RoleAnnouncePayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            std::array<uint8_t, 6> m;
            memcpy(m.data(), mac, 6);
            s1Cascade.push_back(m);
            memcpy(&s1CascadePayload, data, sizeof(s1CascadePayload));
            return 1;
        });

        s1.onRoleAnnounceReceived(suite->opponentMac, macA);
        ASSERT_GE(s1Cascade.size(), 1u);
        // S1's cascade target is its supporter-jack direct peer (supporterMac in fixture).
        EXPECT_EQ(memcmp(s1Cascade[0].data(), suite->supporterMac, 6), 0);
        EXPECT_EQ(memcmp(s1CascadePayload.challengerMac, macA, 6), 0);
    }

    // Leg 2: S2 (end of chain) receives the cascaded announce, caches challengerMac=A.
    // In hardware, S1's MAC would be S2's opponent-jack direct peer. In this shared
    // fixture, S2's opponent-jack (OUTPUT) direct peer is opponentMac, so the announce
    // arrives from opponentMac (standing in for S1 in this single-RDC test).
    {
        PosseManager s2(&suite->player, suite->device.wirelessManager, &suite->rdc);

        s2.onRoleAnnounceReceived(suite->opponentMac, macA);
        ASSERT_NE(s2.getChallengerMac(), nullptr);
        EXPECT_EQ(memcmp(s2.getChallengerMac(), macA, 6), 0);

        // Leg 3: S2 presses button → confirm targets A directly.
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
    }
    // Leg 4: Challenger A receives and records. The originator is two cables away,
    // so A places it from the join it sent, not from any jack of A's own.
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/false);
    PosseManager a(&suite->player, suite->device.wirelessManager, &suite->rdc);
    ASSERT_TRUE(a.isChallenger());

    uint8_t distantMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    a.onChainJoinReceived(distantMac, suite->localMac);
    a.onConfirmReceived(distantMac);
    EXPECT_EQ(a.getBoostMs(), 15u);
}

// When a device was challenger (challengerMac == self-MAC) and gains a same-role
// peer on its opponent jack (becoming a supporter), onChainStateChanged must
// clear challengerMac rather than cascade the stale self-MAC to its supporter.
inline void posseChallengerToSupporterClearsStaleSelfMac(PosseManagerTests* suite) {
    suite->player.setIsHunter(true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    // Allow all sends; connectOutputJack puts a context exchange on the air too.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    // Start as solo challenger — self-assigns challengerMac to localMac.
    posse.onChainStateChanged();
    ASSERT_NE(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(posse.getChallengerMac(), suite->localMac, 6), 0);

    suite->connectOutputJack(/*isHunter=*/true);
    ASSERT_TRUE(posse.isSupporter());

    // onChainStateChanged should detect we hold stale self-MAC and clear it.
    posse.onChainStateChanged();

    EXPECT_EQ(posse.getChallengerMac(), nullptr)
    << "Stale self-MAC was not cleared when challenger was demoted to supporter";
}

inline void posseRoleAnnounceFromSupporterJackIgnoresChallengerMac(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Seed challengerMac via opponent-jack announce first.
    uint8_t realChallenger[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onRoleAnnounceReceived(suite->opponentMac, realChallenger);
    ASSERT_NE(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(posse.getChallengerMac(), realChallenger, 6), 0);

    uint8_t wrongChallenger[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    posse.onRoleAnnounceReceived(suite->supporterMac, wrongChallenger);

    // challengerMac unchanged
    EXPECT_EQ(memcmp(posse.getChallengerMac(), realChallenger, 6), 0);
}

// Role announce from opposite-role opponent-jack peer does NOT update challengerMac.
inline void
posseRoleAnnounceFromOppositeRoleOpponentIgnoresChallengerMac(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);
    uint8_t selfMacArr[6];
    memcpy(selfMacArr, suite->localMac, 6);

    // Seed challengerMac to self via onChainStateChanged (challenger self-assign).
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));
    posse.onChainStateChanged();
    ASSERT_NE(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(posse.getChallengerMac(), selfMacArr, 6), 0);

    uint8_t bountyChallenger[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    posse.onRoleAnnounceReceived(suite->opponentMac, bountyChallenger);

    // challengerMac must remain self (not overwritten with opponent's challenger MAC).
    ASSERT_NE(posse.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(posse.getChallengerMac(), selfMacArr, 6), 0);
}

// After chain break (S1 disconnects from A), S1 becomes challenger
// and updates challengerMac to self.
inline void posseReconfigRecovers(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    suite->deliverPdnContext(suite->opponentMac, /*isHunter=*/true);
    uint8_t macA[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    // S1 starts with challengerMac=A (received from parent).
    PosseManager s1(&suite->player, suite->device.wirelessManager, &suite->rdc);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kRoleAnnounce, _, _))
    .WillRepeatedly(Return(1));

    s1.onRoleAnnounceReceived(suite->opponentMac, macA);
    ASSERT_NE(s1.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(s1.getChallengerMac(), macA, 6), 0);

    // Simulate S1 losing opponent-jack peer by advancing the fake clock past
    // the heartbeat timeout and calling rdc.sync to trigger the disconnect path.
    suite->fakeClock->advance(5000);
    suite->rdc.sync(&suite->device);
    // Now opponent-jack peer is gone; re-evaluate on PosseManager.
    s1.onChainStateChanged();

    ASSERT_TRUE(s1.isChallenger());
    ASSERT_NE(s1.getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(s1.getChallengerMac(), suite->localMac, 6), 0);
}

// COUNTDOWN is fire-and-forget: seqId must be 0 and no retry must fire.
inline void posseGameEventCountdownIsFireAndForget(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    int countdownSends = 0;
    uint8_t seqId = 0xAB;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
    .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t* data, const size_t) {
        ChainGameEventPayload p;
        memcpy(&p, data, sizeof(p));
        if (p.event_type == (uint8_t)ChainGameEventType::COUNTDOWN) {
            seqId = p.seqId;
            countdownSends++;
        }
        return 1;
    });
    posse.sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);
    EXPECT_EQ(countdownSends, 1);
    EXPECT_EQ(seqId, 0u);  // fire-and-forget sentinel

    // sync() after timeout must not retransmit a COUNTDOWN.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainGameEvent, _, _)).Times(0);
    suite->fakeClock->advance(1000);
    posse.sync();
}

// WIN is tracked: seqId != 0, pending registered, retransmit fires on timer.
// The frame is broadcast and carries the challenger's MAC, which is what tells a
// supporter several cables away that the result is from its own match.
inline void posseGameEventWinIsTrackedAndRetried(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

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

    posse.sendGameEventToSupporters(ChainGameEventType::WIN);
    EXPECT_EQ(winSends, 1);
    ASSERT_NE(winSeqId, 0u);
    EXPECT_EQ(memcmp(captured.challengerMac, suite->localMac, 6), 0);

    // Advance past first timeout (100ms), sync() should retransmit once.
    suite->fakeClock->advance(150);
    posse.sync();
    EXPECT_EQ(winSends, 2);
}

// ACK clears pending: no further retransmits after a matching ACK.
inline void posseGameEventAckClearsPending(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

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

    posse.sendGameEventToSupporters(ChainGameEventType::LOSS);
    ASSERT_NE(winSeqId, 0u);

    posse.onChainGameEventAckReceived(suite->supporterMac, winSeqId);

    // After ACK, sync past timeout must not retransmit.
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainGameEvent, _, _)).Times(0);
    suite->fakeClock->advance(1000);
    posse.sync();
}

// Once a frame's retransmit span runs out with no ACK it is abandoned — no further sends.
inline void posseGameEventAbandonsWhenSpanRunsOut(PosseManagerTests* suite) {
    suite->setupHunterChallenger();
    PosseManager posse(&suite->player, suite->device.wirelessManager, &suite->rdc);

    int supporterSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
    .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
        if (memcmp(mac, MockDevice::BROADCAST_MAC, 6) == 0) supporterSends++;
        return 1;
    });

    posse.sendGameEventToSupporters(ChainGameEventType::WIN);
    ASSERT_EQ(supporterSends, 1);

    // The retransmits land 100, 300 and 700ms in (gaps of 100, 200, 400); the next
    // would fall past the
    // frame's span, so that is where it stops.
    runLoopFor(suite, posse, Resender::RETRANSMIT_SPAN_MS - 100);
    EXPECT_EQ(supporterSends, 4);  // 1 initial + 3 retransmits

    // After abandon, further sync must not retransmit.
    suite->fakeClock->advance(1000);
    posse.sync();
    EXPECT_EQ(supporterSends, 4);

    EXPECT_EQ(posse.getRetryStats().abandons, 1u);
}
