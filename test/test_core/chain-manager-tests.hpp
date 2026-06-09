#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/mac-types.hpp"
#include "device/device-type.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/chain-manager.hpp"
#include "game/player.hpp"

#include <algorithm>
#include <optional>

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

// Injection RDC for ChainManager unit tests. ChainManager derives every role
// decision from a small set of RDC queries: getChampionMac (the BEACON-flood
// walk), mutualOpponentIsHunter, isInLoop, isTopologyStable, getChainMembers,
// countChainBehind, getPeerMac. The walk itself is exercised in PeerGraphTests;
// here we shape its result directly so each test pins exactly the chain-manager
// behavior under test, with no serial HELLO/BEACON machinery in the way.
class FakeTopologyRDC : public RemoteDeviceCoordinator {
public:
    std::vector<net::Mac> members;
    bool inLoop = false;
    bool stable = true;
    size_t chainBehind = 0;
    std::optional<net::Mac> champion;       // result of the peer-graph champion walk
    std::optional<bool> oppIsHunter;        // hunter/bounty role of the opponent-jack mutual peer

    std::vector<net::Mac> getChainMembers() const override { return members; }
    bool isInLoop() const override { return inLoop; }
    bool isTopologyStable() const override { return stable; }
    size_t countChainBehind(SerialIdentifier) const override { return chainBehind; }
    std::optional<net::Mac> getChampionMac() const override { return champion; }
    std::optional<bool> mutualOpponentIsHunter() const override { return oppIsHunter; }

    void setPeerMac(SerialIdentifier id, const uint8_t* mac) {
        bool& set = (id == SerialIdentifier::OUTPUT_JACK) ? outSet_ : inSet_;
        uint8_t* slot = (id == SerialIdentifier::OUTPUT_JACK) ? outMac_ : inMac_;
        set = (mac != nullptr);
        if (mac) memcpy(slot, mac, 6);
    }
    const uint8_t* getPeerMac(SerialIdentifier id) const override {
        if (id == SerialIdentifier::OUTPUT_JACK && outSet_) return outMac_;
        if (id == SerialIdentifier::INPUT_JACK && inSet_) return inMac_;
        return nullptr;
    }

private:
    uint8_t inMac_[6] = {};
    uint8_t outMac_[6] = {};
    bool inSet_ = false;
    bool outSet_ = false;
};

class ChainManagerTests : public testing::Test {
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
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    net::Mac selfMacArr() const {
        net::Mac m; std::copy_n(localMac, 6, m.begin()); return m;
    }
    net::Mac macOf(const uint8_t* p) const {
        net::Mac m; std::copy_n(p, 6, m.begin()); return m;
    }

    MockDevice device;
    FakePlatformClock* fakeClock;
    Player player;

    uint8_t localMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t opponentMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t supporterMac[6] = {0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
};

// getChainLength reports the supporter-side depth for a champion and zero for
// anyone else: the posse belongs to the device leading it, not to a mid-chain
// supporter that happens to have its own downstream peers.
inline void chainGetChainLengthChampionOnly(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);

    FakeTopologyRDC champRdc;
    champRdc.stable = true;
    champRdc.inLoop = false;
    champRdc.chainBehind = 3;
    champRdc.champion = suite->selfMacArr();          // self is the head
    ChainManager champ(&suite->player, suite->device.wirelessManager, &champRdc);
    ASSERT_TRUE(champ.isChampion());
    EXPECT_EQ(champ.getChainLength(), 3u);

    FakeTopologyRDC supRdc;
    supRdc.chainBehind = 3;
    supRdc.champion = suite->macOf(suite->opponentMac);  // head is someone else
    ChainManager supporter(&suite->player, suite->device.wirelessManager, &supRdc);
    ASSERT_FALSE(supporter.isChampion());
    EXPECT_EQ(supporter.getChainLength(), 0u);
}

// Role derivation: an injected champion-of-self topology yields champion + can
// initiate; with no opponent everything is false/empty.
inline void chainRoleDerivationWithChampionTopology(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;

    // No champion resolved yet (e.g. cold boot): everything off.
    ChainManager cold(&suite->player, suite->device.wirelessManager, &fakeRdc);
    EXPECT_FALSE(cold.isChampion());
    EXPECT_FALSE(cold.isSupporter());
    EXPECT_FALSE(cold.canInitiateMatch());
    EXPECT_TRUE(cold.getSupporterChainPeers().empty());

    // Full champion: self is head, a bounty sits on the opponent jack, a
    // supporter on the supporter jack (INPUT for a hunter).
    fakeRdc.champion = suite->selfMacArr();
    fakeRdc.oppIsHunter = false;          // bounty opponent
    fakeRdc.stable = true;
    fakeRdc.inLoop = false;
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, suite->supporterMac);
    ChainManager champ(&suite->player, suite->device.wirelessManager, &fakeRdc);

    EXPECT_TRUE(champ.isChampion());
    EXPECT_FALSE(champ.isSupporter());
    EXPECT_TRUE(champ.canInitiateMatch());
    EXPECT_FALSE(champ.getSupporterChainPeers().empty());
}

// Bounties never initiate matches regardless of topology.
inline void chainCanInitiateMatchFalseForBounty(ChainManagerTests* suite) {
    suite->player.setIsHunter(false);
    FakeTopologyRDC fakeRdc;
    fakeRdc.oppIsHunter = true;   // a hunter on the opponent jack
    fakeRdc.stable = true;
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    EXPECT_FALSE(chainManager.canInitiateMatch());
}

// Loop detected: even a hunter with a bounty on their opponent jack must NOT
// initiate a duel; the Idle → ShootoutProposal path owns the next transition.
inline void chainCanInitiateMatchFalseWhenInLoop(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    fakeRdc.stable = true;
    fakeRdc.inLoop = false;
    fakeRdc.oppIsHunter = false;   // bounty on the opponent jack: a valid pairing
    suite->player.setIsHunter(true);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);

    ASSERT_TRUE(chainManager.canInitiateMatch())
        << "precondition: stable, not in loop, hunter with opposite-role opponent";

    fakeRdc.inLoop = true;
    EXPECT_FALSE(chainManager.canInitiateMatch())
        << "in a loop, no member may start a 1v1; shootout owns the transition";
}

// Confirm lifecycle at the champion: starts at 0, increments per unique MAC,
// dedup blocks repeats, clear resets.
inline void chainConfirmLifecycle(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    fakeRdc.stable = true;
    fakeRdc.champion = suite->selfMacArr();
    // Supporter jack for a hunter is INPUT; the confirming supporter is our
    // direct peer there (satisfies the membership gate).
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, suite->supporterMac);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    ASSERT_TRUE(chainManager.isChampion());

    EXPECT_EQ(chainManager.getBoostMs(), 0u);

    chainManager.onConfirmReceived(suite->supporterMac, suite->supporterMac, 1);
    EXPECT_EQ(chainManager.getBoostMs(), 15u);
    EXPECT_EQ(chainManager.getConfirmedSupporterCount(), 1u);

    // Same originator, different seqId → deduped at champion.
    chainManager.onConfirmReceived(suite->supporterMac, suite->supporterMac, 2);
    EXPECT_EQ(chainManager.getConfirmedSupporterCount(), 1u);

    chainManager.clearSupporterConfirms();
    EXPECT_EQ(chainManager.getBoostMs(), 0u);
    EXPECT_EQ(chainManager.getConfirmedSupporterCount(), 0u);
}

// onChainStateChanged drains confirms when the supporter-jack peer disappears.
inline void chainOnChainStateChangedClearsOnDrain(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    fakeRdc.stable = true;
    fakeRdc.champion = suite->selfMacArr();
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, suite->supporterMac);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    ASSERT_TRUE(chainManager.isChampion());

    chainManager.onConfirmReceived(suite->supporterMac, suite->supporterMac, 1);
    ASSERT_EQ(chainManager.getBoostMs(), 15u);

    // Supporter-jack peer still present: retained.
    chainManager.onChainStateChanged();
    EXPECT_EQ(chainManager.getBoostMs(), 15u);

    // Peer gone: drained.
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, nullptr);
    chainManager.onChainStateChanged();
    EXPECT_EQ(chainManager.getBoostMs(), 0u);
    EXPECT_EQ(chainManager.getConfirmedSupporterCount(), 0u);
}

// A device whose champion-walk resolves to someone else is a supporter, not a
// champion.
inline void chainIsChampionFalseWithSameRoleOpponent(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    fakeRdc.champion = suite->macOf(suite->opponentMac);  // head is upstream, not us
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    EXPECT_TRUE(chainManager.isSupporter());
    EXPECT_FALSE(chainManager.isChampion());
}

// The isCoordinator_ guard is load-bearing: even when the champion walk
// resolves to a non-self MAC (which would otherwise make this device a
// supporter), a device that has claimed coordinator reports neither supporter
// nor champion, so its 1Hz confirm backstop never fires a stray ChainConfirm.
inline void chainCoordinatorGuardSuppressesSupporter(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    fakeRdc.champion = suite->macOf(suite->opponentMac);  // walk says: supporter
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    ASSERT_TRUE(chainManager.isSupporter()) << "precondition: would be a supporter";

    chainManager.claimCoordinator();
    EXPECT_FALSE(chainManager.isSupporter()) << "coordinator must never be a supporter";
    EXPECT_FALSE(chainManager.isChampion()) << "coordinator must never be a champion";
}

// Coord election is topology-derived inside sync(); a bare onDirectPeerChange
// firing must never claim.
inline void chainDirectPeerConnectDoesNotClaim(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);
    ASSERT_FALSE(chainManager.isCoordinator());

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));

    std::array<uint8_t, 6> someMac = {0x99, 0x99, 0x99, 0x99, 0x99, 0x99};
    RemoteDeviceCoordinator::Peer peer{someMac, DeviceType::PDN};
    chainManager.onDirectPeerChange(SerialIdentifier::OUTPUT_JACK,
                           std::nullopt,
                           std::optional<RemoteDeviceCoordinator::Peer>(peer));

    EXPECT_FALSE(chainManager.isCoordinator());
}

// sendConfirm targets the derived champion with a ChainConfirmPayload carrying
// our own MAC as originator.
inline void chainSendConfirmTargetsChampionMac(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    fakeRdc.champion = suite->macOf(suite->opponentMac);   // champion is upstream
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);

    std::array<uint8_t, 6> target{};
    ChainConfirmPayload captured{};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            memcpy(target.data(), mac, 6);
            memcpy(&captured, data, sizeof(captured));
            return 1;
        });

    EXPECT_TRUE(chainManager.sendConfirm());

    EXPECT_EQ(memcmp(target.data(), suite->opponentMac, 6), 0);
    EXPECT_EQ(memcmp(captured.originatorMac, suite->localMac, 6), 0);
}

// sendConfirm increments seqId on each call.
inline void chainSendConfirmIncrementsSeqId(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    fakeRdc.champion = suite->macOf(suite->opponentMac);
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);

    std::vector<uint8_t> seqIds;
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, sizeof(ChainConfirmPayload)))
        .WillRepeatedly([&](const uint8_t*, PktType, const uint8_t* data, const size_t) {
            ChainConfirmPayload p; memcpy(&p, data, sizeof(p));
            seqIds.push_back(p.seqId);
            return 1;
        });

    chainManager.sendConfirm();
    chainManager.sendConfirm();
    chainManager.sendConfirm();
    ASSERT_EQ(seqIds.size(), 3u);
    EXPECT_EQ(seqIds[1], static_cast<uint8_t>(seqIds[0] + 1));
    EXPECT_EQ(seqIds[2], static_cast<uint8_t>(seqIds[0] + 2));
}

// sendConfirm is a noop when no champion is resolved, and reports the skip so
// the supporter press handler doesn't latch hasConfirmed on nothing.
inline void chainSendConfirmNoopWhenChampionMacInvalid(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;   // champion stays nullopt
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainConfirm, _, _)).Times(0);

    EXPECT_FALSE(chainManager.sendConfirm());
}

// Helper: stand up a champion with one supporter-jack peer, ready to send game
// events to supporters.
inline ChainManager makeChampionWithSupporter(ChainManagerTests* suite,
                                              FakeTopologyRDC& fakeRdc,
                                              WirelessTransport& transport) {
    suite->player.setIsHunter(true);
    fakeRdc.stable = true;
    fakeRdc.champion = suite->selfMacArr();
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, suite->supporterMac);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);
    return chainManager;
}

// COUNTDOWN is reliable: a dropped one would leave a supporter un-armed for the
// whole duel (shows "Missed"), so it gets a seqId and retransmits on timeout
// just like WIN/LOSS. SupersedePerTarget keeps a late retry from re-arming after
// the duel resolves.
inline void chainGameEventCountdownIsTrackedAndRetried(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager = makeChampionWithSupporter(suite, fakeRdc, transport);

    int supporterSends = 0;
    uint8_t seqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                ChainGameEventPayload p; memcpy(&p, data, sizeof(p));
                if (p.event_type == (uint8_t)ChainGameEventType::COUNTDOWN) {
                    seqId = p.seqId;
                    supporterSends++;
                }
            }
            return 1;
        });

    chainManager.sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);
    EXPECT_EQ(supporterSends, 1);
    ASSERT_NE(seqId, 0u);

    // No ack arrives: the retry fires on the next timeout.
    suite->fakeClock->advance(150);
    transport.sync();
    EXPECT_EQ(supporterSends, 2);
}

// COUNTDOWN must reach multi-hop supporters, not just the direct supporter-jack
// peer. A 2-hop supporter is addressable only through confirmedSupporters_;
// clearing that tally before sending drops it from the COUNTDOWN, so it never
// arms, can't press, and shows "Missed" even while its steady-state confirm
// still pads the champion's boost. The send must happen before the clear.
inline void chainCountdownReachesMultiHopSupporter(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    uint8_t farSupporter[6] = {0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};
    fakeRdc.members = { suite->macOf(farSupporter) };  // a known member, two hops out
    ChainManager chainManager = makeChampionWithSupporter(suite, fakeRdc, transport);

    // The far supporter confirms up the chain (its 1Hz backstop), so the champion
    // holds it in confirmedSupporters_, the only way it can address it.
    chainManager.onConfirmReceived(farSupporter, farSupporter, 1);

    bool farGotCountdown = false;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            ChainGameEventPayload p; memcpy(&p, data, sizeof(p));
            if (p.event_type == (uint8_t)ChainGameEventType::COUNTDOWN &&
                memcmp(mac, farSupporter, 6) == 0) {
                farGotCountdown = true;
            }
            return 1;
        });

    chainManager.sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);
    EXPECT_TRUE(farGotCountdown)
        << "multi-hop supporter must receive COUNTDOWN (send before clearing confirms)";
}

// WIN is tracked: seqId != 0, pending registered, retransmit fires on timer.
inline void chainGameEventWinIsTrackedAndRetried(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager = makeChampionWithSupporter(suite, fakeRdc, transport);

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

    chainManager.sendGameEventToSupporters(ChainGameEventType::WIN);
    EXPECT_EQ(supporterSends, 1);
    ASSERT_NE(winSeqId, 0u);

    suite->fakeClock->advance(150);
    transport.sync();
    EXPECT_EQ(supporterSends, 2);
}

// ACK clears pending: no further retransmits after a matching ACK.
inline void chainGameEventAckClearsPending(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager = makeChampionWithSupporter(suite, fakeRdc, transport);

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

    chainManager.sendGameEventToSupporters(ChainGameEventType::LOSS);
    ASSERT_NE(winSeqId, 0u);

    {
        AckPayload ack{static_cast<uint8_t>(PktType::kChainGameEvent), 0, winSeqId};
        transport.onAckPacket(suite->supporterMac,
            reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    }

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, _)).Times(0);
    suite->fakeClock->advance(1000);
    transport.sync();
}

// ACK with the right seqId but wrong source MAC must NOT clear pending; the
// resender still retransmits to the real supporter.
inline void chainGameEventAckFromWrongMacIgnored(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager = makeChampionWithSupporter(suite, fakeRdc, transport);

    uint8_t winSeqId = 0;
    int supporterSends = 0;
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

    chainManager.sendGameEventToSupporters(ChainGameEventType::WIN);
    ASSERT_EQ(supporterSends, 1);
    ASSERT_NE(winSeqId, 0u);

    uint8_t forgedMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00};
    {
        AckPayload ack{static_cast<uint8_t>(PktType::kChainGameEvent), 0, winSeqId};
        transport.onAckPacket(forgedMac,
            reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    }

    suite->fakeClock->advance(150);
    transport.sync();
    EXPECT_EQ(supporterSends, 2) << "forged ack must not suppress the retransmit";
}

// After kMaxRetries with no ACK, pending is abandoned (no further sends) and
// the retry stats record the abandon.
inline void chainGameEventAbandonsAfterMax(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager = makeChampionWithSupporter(suite, fakeRdc, transport);

    int supporterSends = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t*, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) supporterSends++;
            return 1;
        });

    chainManager.sendGameEventToSupporters(ChainGameEventType::WIN);
    ASSERT_EQ(supporterSends, 1);

    for (int i = 0; i < 3; i++) {
        suite->fakeClock->advance(1000);
        transport.sync();
    }
    EXPECT_EQ(supporterSends, 4);  // 1 initial + 3 retransmits

    suite->fakeClock->advance(1000);
    transport.sync();
    EXPECT_EQ(supporterSends, 4);

    EXPECT_EQ(chainManager.getRetryStats().abandons, 1u);
}

// RetryStats aggregates sends, retries, acks, and abandons across the channels
// ChainManager owns. Observability for hardware-validation tuning.
inline void chainRetryStatsRecordsLifecycle(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager = makeChampionWithSupporter(suite, fakeRdc, transport);

    uint8_t winSeqId = 0;
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(_, PktType::kChainGameEvent, _, sizeof(ChainGameEventPayload)))
        .WillRepeatedly([&](const uint8_t* mac, PktType, const uint8_t* data, const size_t) {
            if (memcmp(mac, suite->supporterMac, 6) == 0) {
                ChainGameEventPayload p; memcpy(&p, data, sizeof(p));
                if (p.seqId != 0) winSeqId = p.seqId;
            }
            return 1;
        });

    // One reliable send, then an ack clears it.
    chainManager.sendGameEventToSupporters(ChainGameEventType::WIN);
    ASSERT_NE(winSeqId, 0u);
    suite->fakeClock->advance(50);
    {
        AckPayload ack{static_cast<uint8_t>(PktType::kChainGameEvent), 0, winSeqId};
        transport.onAckPacket(suite->supporterMac,
            reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    }

    auto s1 = chainManager.getRetryStats();
    EXPECT_GE(s1.sends, 1u);
    EXPECT_EQ(s1.abandons, 0u);
    EXPECT_GE(s1.ackCount, 1u);
    EXPECT_GT(s1.ackLatencyMsSum, 0u);

    // Force a retransmit then exhaust retries → abandons increments.
    chainManager.sendGameEventToSupporters(ChainGameEventType::LOSS);
    for (int i = 0; i < 7; i++) {
        suite->fakeClock->advance(2000);
        transport.sync();
    }
    auto s2 = chainManager.getRetryStats();
    EXPECT_GE(s2.retries, 3u);
    EXPECT_GE(s2.abandons, 1u);
}

// =====================================================================
// Topology-derived coord election with 1-cycle stability guard.
// =====================================================================

// Self with lowest MAC + isInLoop + min stable for ≥1 cycle → claim coord.
inline void chainClaimsCoordinatorWhenSelfIsLowestMacInLoop(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    net::Mac self = suite->selfMacArr();
    net::Mac higher = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x01};
    fakeRdc.members = {self, higher};
    fakeRdc.inLoop = true;

    chainManager.sync();
    EXPECT_FALSE(chainManager.isCoordinator()) << "Cycle 0 seeds the value; must not claim yet";

    suite->fakeClock->advance(1100);
    chainManager.sync();
    EXPECT_TRUE(chainManager.isCoordinator()) << "After 1s of stability, self lowest → claim";
}

// A lower-MAC peer arriving among the chain members must demote a sitting coordinator.
inline void chainDemotesCoordinatorWhenLowerMacJoins(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    net::Mac self = suite->selfMacArr();
    net::Mac higher = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x01};
    fakeRdc.members = {self, higher};
    fakeRdc.inLoop = true;

    chainManager.sync();
    suite->fakeClock->advance(1100);
    chainManager.sync();
    ASSERT_TRUE(chainManager.isCoordinator());

    net::Mac lower = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    fakeRdc.members = {self, higher, lower};

    suite->fakeClock->advance(1100);
    chainManager.sync();
    EXPECT_FALSE(chainManager.isCoordinator())
        << "Lower-MAC peer arrival must demote; defers coord to that peer";
}

// Linear chain (isInLoop=false): never claim coord regardless of MAC ordering.
inline void chainDoesNotClaimWithoutLoop(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    net::Mac self = suite->selfMacArr();
    net::Mac higher = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x01};
    fakeRdc.members = {self, higher};
    fakeRdc.inLoop = false;

    for (int i = 0; i < 5; ++i) {
        suite->fakeClock->advance(1100);
        chainManager.sync();
    }
    EXPECT_FALSE(chainManager.isCoordinator()) << "Linear chain must never produce a coord";
}

// Min-MAC flips within the cycle window. The 1-cycle stability guard must
// prevent any coord claim during the churn.
inline void chainOneSecondMinStabilityGuard(ChainManagerTests* suite) {
    FakeTopologyRDC fakeRdc;
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    net::Mac self = suite->selfMacArr();
    net::Mac lower = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    net::Mac higher = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x01};
    fakeRdc.inLoop = true;

    fakeRdc.members = {self, higher};
    chainManager.sync();
    EXPECT_FALSE(chainManager.isCoordinator()) << "Cycle 0 seeds, never claims";

    suite->fakeClock->advance(1100);
    fakeRdc.members = {self, higher, lower};
    chainManager.sync();
    EXPECT_FALSE(chainManager.isCoordinator()) << "min flipped; counter resets, no claim";

    suite->fakeClock->advance(1100);
    fakeRdc.members = {self, higher};
    chainManager.sync();
    EXPECT_FALSE(chainManager.isCoordinator())
        << "Stability guard: min just flipped back, cycle counter is fresh";

    suite->fakeClock->advance(1100);
    chainManager.sync();
    EXPECT_TRUE(chainManager.isCoordinator()) << "Min stable for one cycle → claim allowed";
}

// onConfirmReceived applies the isTopologyStable gate unconditionally: a confirm
// arriving while the topology is still settling is deferred until it holds.
inline void chainConfirmDroppedWhenTopologyUnstable(ChainManagerTests* suite) {
    suite->player.setIsHunter(true);
    FakeTopologyRDC fakeRdc;
    // The confirming device is a ring member (membership gate is reached before
    // the stability gate).
    net::Mac supMember; std::copy_n(suite->supporterMac, 6, supMember.begin());
    fakeRdc.members.push_back(supMember);
    fakeRdc.champion = suite->selfMacArr();
    WirelessTransport transport(suite->device.wirelessManager);
    ChainManager chainManager(&suite->player, suite->device.wirelessManager, &fakeRdc);
    chainManager.initialize(&transport);

    fakeRdc.inLoop = true;
    fakeRdc.stable = false;
    chainManager.onConfirmReceived(suite->supporterMac, suite->supporterMac, 1);
    EXPECT_EQ(chainManager.getConfirmedSupporterCount(), 0u)
        << "In a ring, confirms must be deferred while topology is unstable";

    fakeRdc.stable = true;
    chainManager.onConfirmReceived(suite->supporterMac, suite->supporterMac, 1);
    EXPECT_EQ(chainManager.getConfirmedSupporterCount(), 1u)
        << "Once stable, the ring's confirm is accepted";
}

// =====================================================================
// chain_role walk tests: the game-layer champion walk and opponent-role
// query over a raw PeerGraph. The graph floods the role byte opaquely;
// these pin the interpretation (1=HUNTER, 2=BOUNTY, 0=NONE) and the walk
// semantics, duelist and non-duelist alike.
// =====================================================================

inline net::Mac chainRoleMac(uint8_t last) {
    return {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, last};
}

// A lone hunter with both jacks open is its own champion.
inline void chainRoleLoneHunterIsSelfChampion() {
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x01));
    g.setSelfRole(chain_role::byteFor(true));
    g.setSelfPeers({}, {}, 0);
    auto champ = chain_role::championMac(g);
    ASSERT_TRUE(champ.has_value());
    EXPECT_EQ(*champ, chainRoleMac(0x01));
}

// Hunter with a bounty on its opponent (OUTPUT) jack: the hunter is the head of
// its own one-node chain, and the opponent reads as opposite-role.
inline void chainRoleChampionHunterFacingBounty() {
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x01));
    g.setSelfRole(chain_role::byteFor(true));
    g.setSelfPeers(/*in*/{}, /*out*/chainRoleMac(0x02), 0);
    BeaconRecord bounty{chainRoleMac(0x02), chainRoleMac(0x01), {}};  // in=self, out=open
    bounty.role = chain_role::byteFor(false);
    g.acceptBeacon(bounty, 0);
    auto champ = chain_role::championMac(g);
    ASSERT_TRUE(champ.has_value());
    EXPECT_EQ(*champ, chainRoleMac(0x01));  // self is the hunter-chain head
    auto opp = chain_role::mutualOpponentIsHunter(g);
    ASSERT_TRUE(opp.has_value());
    EXPECT_FALSE(*opp);  // opponent is a bounty
}

// A mid-chain hunter supporter walks its opponent (OUTPUT) jack up to the head.
// Chain: champion C(0x01).in=S1; S1(0x02).out=C,in=S2; S2(0x03)=self.out=S1.
inline void chainRoleChampionWalkToHead() {
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x03));   // self is S2, the farthest supporter
    g.setSelfRole(chain_role::byteFor(true));
    g.setSelfPeers(/*in*/{}, /*out*/chainRoleMac(0x02), 0);
    BeaconRecord s1{chainRoleMac(0x02), chainRoleMac(0x03), chainRoleMac(0x01)};  // in=S2, out=C
    s1.role = chain_role::byteFor(true);
    g.acceptBeacon(s1, 0);
    BeaconRecord c{chainRoleMac(0x01), chainRoleMac(0x02), {}};  // in=S1, out=open
    c.role = chain_role::byteFor(true);
    g.acceptBeacon(c, 0);
    auto champ = chain_role::championMac(g);
    ASSERT_TRUE(champ.has_value());
    EXPECT_EQ(*champ, chainRoleMac(0x01));  // every supporter resolves to C
}

// A closed same-role ring has no champion: championMac is nullopt so no node
// claims champion/supporter; the ring arms a shootout instead.
inline void chainRoleChampionNoneInClosedRing() {
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x01));
    g.setSelfRole(chain_role::byteFor(true));
    g.setSelfPeers(chainRoleMac(0x02), chainRoleMac(0x03), 0);
    BeaconRecord b2{chainRoleMac(0x02), chainRoleMac(0x03), chainRoleMac(0x01)};
    b2.role = chain_role::byteFor(true);
    g.acceptBeacon(b2, 0);
    BeaconRecord b3{chainRoleMac(0x03), chainRoleMac(0x01), chainRoleMac(0x02)};
    b3.role = chain_role::byteFor(true);
    g.acceptBeacon(b3, 0);
    ASSERT_TRUE(g.isInLoop());
    EXPECT_FALSE(chain_role::championMac(g).has_value());
}

// The half-open-ring regression. Four hunters wired as a ring, but one link is
// non-mutual (A claims no INPUT peer, while D still claims A on its OUTPUT). The
// ring never closes, so every node must instead resolve the same open-chain
// champion: the node on the open side of the broken link (D).
inline void chainRoleChampionResolvesInHalfOpenRing() {
    // Directed ring intent: out=up-chain, in=down-chain.
    //   A(0x01): in=open (broken back-claim), out=B
    //   B(0x02): in=A,  out=C
    //   C(0x03): in=B,  out=D   <- self
    //   D(0x04): in=C,  out=A   (claims A, but A does not claim back)
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x03));   // self is C
    g.setSelfRole(chain_role::byteFor(true));
    g.setSelfPeers(/*in*/chainRoleMac(0x02), /*out*/chainRoleMac(0x04), 0);
    BeaconRecord a{chainRoleMac(0x01), {}, chainRoleMac(0x02)};
    a.role = chain_role::byteFor(true);
    BeaconRecord b{chainRoleMac(0x02), chainRoleMac(0x01), chainRoleMac(0x03)};
    b.role = chain_role::byteFor(true);
    BeaconRecord d{chainRoleMac(0x04), chainRoleMac(0x03), chainRoleMac(0x01)};
    d.role = chain_role::byteFor(true);
    g.acceptBeacon(a, 0);
    g.acceptBeacon(b, 0);
    g.acceptBeacon(d, 0);
    EXPECT_FALSE(g.isInLoop());  // half-open: not a closed cycle
    auto champ = chain_role::championMac(g);
    ASSERT_TRUE(champ.has_value());
    EXPECT_EQ(*champ, chainRoleMac(0x04));  // D, on the open side of the broken link
}

// A non-duelist self (FDN, or a PDN whose role provider never ran) has no
// champion: it holds no place in any duel chain, so the walk yields nullopt
// rather than treating the default 0 byte as "bounty".
inline void chainRoleNoneSelfHasNoChampion() {
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x01));
    // No setSelfRole call: the never-provided default must read as NONE.
    g.setSelfPeers(/*in*/chainRoleMac(0x02), /*out*/{}, 0);
    BeaconRecord b{chainRoleMac(0x02), {}, chainRoleMac(0x01)};
    b.role = chain_role::byteFor(true);
    g.acceptBeacon(b, 0);
    EXPECT_FALSE(chain_role::championMac(g).has_value());
}

// FDN mid-chain. Bounty chain with a NONE node spliced in:
//   self B(0x01).in = FDN(0x02); FDN.in = B'(0x03); B'.in = open.
// The NONE node severs the chain: self is its own champion, the FDN is never
// a candidate, and the walk must not cross it to B' as if 0 were "bounty".
inline void chainRoleNoneMidChainSeversChain() {
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x01));
    g.setSelfRole(chain_role::byteFor(false));        // self is a bounty
    g.setSelfPeers(/*in*/chainRoleMac(0x02), /*out*/{}, 0);
    BeaconRecord fdn{chainRoleMac(0x02), chainRoleMac(0x03), chainRoleMac(0x01)};
    fdn.role = static_cast<uint8_t>(chain_role::Role::NONE);
    g.acceptBeacon(fdn, 0);
    BeaconRecord far{chainRoleMac(0x03), {}, chainRoleMac(0x02)};
    far.role = chain_role::byteFor(false);            // a bounty beyond the FDN
    g.acceptBeacon(far, 0);

    auto champ = chain_role::championMac(g);
    ASSERT_TRUE(champ.has_value());
    EXPECT_EQ(*champ, chainRoleMac(0x01))
        << "the duel chain must end at the last duelist before the FDN";
}

// A NONE peer on the opponent jack is not a duel opponent: the query must be
// nullopt, not "bounty"; this is what fails match-initiation closed against
// an FDN at the game layer, independent of Idle's deviceType gate.
inline void chainRoleMutualOpponentNoneIsNotAnOpponent() {
    PeerGraph g;
    g.setSelfMac(chainRoleMac(0x01));
    g.setSelfRole(chain_role::byteFor(true));         // hunter
    g.setSelfPeers(/*in*/{}, /*out*/chainRoleMac(0x02), 0);
    BeaconRecord fdn{chainRoleMac(0x02), chainRoleMac(0x01), {}};
    fdn.role = static_cast<uint8_t>(chain_role::Role::NONE);
    g.acceptBeacon(fdn, 0);
    EXPECT_FALSE(chain_role::mutualOpponentIsHunter(g).has_value());
}
