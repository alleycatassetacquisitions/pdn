#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/shootout-manager.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/quickdraw-states.hpp"
#include "game/player.hpp"

class ShootoutManagerTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
            .WillByDefault(testing::Return(1));
        ON_CALL(*device.mockPeerComms, addEspNowPeer(testing::_)).WillByDefault(testing::Return(0));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(localMac));

        rdc.initialize(device.wirelessManager, device.serialManager, &device);
        shootout = new ShootoutManager(&player, device.wirelessManager, &rdc);
    }

    void TearDown() override {
        delete shootout;
        shootout = nullptr;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    Player player{"TEST", Allegiance::RESISTANCE, true};
    ShootoutManager* shootout = nullptr;
    FakePlatformClock* fakeClock = nullptr;
    uint8_t localMac[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
};

inline void localConfirmIsRecordedAndBroadcast(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    suite->shootout->setLoopMembersForTest({
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}
    });
    suite->shootout->startProposal();
    EXPECT_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::Return(1));
    suite->shootout->confirmLocal();
    EXPECT_EQ(suite->shootout->getConfirmedCount(), 1u);
    EXPECT_TRUE(suite->shootout->hasConfirmed(selfMac));
}

inline void confirmRebroadcastsEverySecondDuringProposal(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    suite->shootout->setLoopMembersForTest({
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}
    });
    EXPECT_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(1));
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();

    for (int i = 0; i < 30; i++) {
        suite->fakeClock->advance(100);
        suite->shootout->sync();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
}

// Coordinator is whoever the RDC handed the ring-closed event to, not whoever
// holds the lowest MAC: self here is the lowest of the three and still is not
// coordinator until the claim.
inline void coordinatorIsTheRingClosureClaimant(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    std::array<uint8_t, 6> a = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c = {0x03, 0, 0, 0, 0, 0};
    suite->shootout->setLoopMembersForTest({a, b, c});
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();                   // adds self (0x01)
    suite->shootout->onConfirmReceived(a.data());      // adds 0x05
    suite->shootout->onConfirmReceived(c.data());      // adds 0x03
    EXPECT_FALSE(suite->shootout->isCoordinator())
        << "lowest MAC must not elect itself";

    // The same device once its RDC reports the ring closed on its own in-jack.
    suite->shootout->resetToIdle();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    EXPECT_TRUE(suite->shootout->isCoordinator());
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), b.data(), 6), 0);
}

// The head announces the roster it just claimed, and that broadcast is the only
// thing that opens the proposal window on the other members.
inline void ringClosedClaimAnnouncesRosterToMembers(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}};
    suite->shootout->setLoopMembersForTest(members);

    std::vector<uint8_t> frame;
    std::array<uint8_t, 6> destination{};
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&frame, &destination](const uint8_t* dst, PktType, const uint8_t* data,
                                   const size_t len) {
                memcpy(destination.data(), dst, 6);
                frame.assign(data, data + len);
                return 1;
            }));

    EXPECT_FALSE(suite->shootout->shouldEnterProposal());
    suite->shootout->onRingClosed();
    EXPECT_TRUE(suite->shootout->shouldEnterProposal());

    EXPECT_EQ(memcmp(destination.data(), MockDevice::BROADCAST_MAC, 6), 0);
    ASSERT_EQ(frame.size(), 3u + 6u * members.size());
    EXPECT_EQ(frame[0], static_cast<uint8_t>(ShootoutCmd::RING_CLOSED));
    EXPECT_EQ(frame[2], members.size());
    for (size_t i = 0; i < members.size(); i++) {
        EXPECT_EQ(memcmp(&frame[3 + 6 * i], members[i].data(), 6), 0) << "member " << i;
    }
}

// A member has no local ring signal to poll: the coordinator's broadcast is
// what puts it into proposal, and a roster it is absent from belongs to a
// neighbouring ring.
inline void ringClosedBroadcastPromotesOnlyItsOwnMembers(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    std::array<uint8_t, 6> alienCoord = {0xA1, 0, 0, 0, 0, 0};
    suite->shootout->onRingClosedReceived(alienCoord.data(), {alienCoord, other});
    EXPECT_FALSE(suite->shootout->shouldEnterProposal());
    EXPECT_TRUE(suite->shootout->getLoopMembers().empty());

    suite->shootout->onRingClosedReceived(coord.data(), {coord, me, other});
    EXPECT_TRUE(suite->shootout->shouldEnterProposal());
    EXPECT_FALSE(suite->shootout->isCoordinator());
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), coord.data(), 6), 0);
    EXPECT_EQ(suite->shootout->getLoopMembers().size(), 3u);

    // The member's own CONFIRM now has a ring to reach; an empty roster would
    // have suppressed it at the broadcast guard.
    suite->shootout->startProposal();
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::Return(1));
    suite->shootout->confirmLocal();
}

// The coordinator's roster is the RDC's head-only chain-member set plus itself:
// getChainMembers() enumerates who announced to the head, never the head.
inline void ringHeadLoopMembersComeFromRdcRoster(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    FakeRingRemoteDeviceCoordinator ringRdc;
    ringRdc.chainMembers = {{0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}};
    ShootoutManager ringShootout(&suite->player, suite->device.wirelessManager, &ringRdc);

    std::vector<std::array<uint8_t, 6>> members = ringShootout.getLoopMembers();
    ASSERT_EQ(members.size(), 3u);
    EXPECT_EQ(memcmp(members[2].data(), selfMac, 6), 0);

    // A device the RDC does not treat as the ring's roster authority has nothing
    // to read and falls back to whatever the coordinator broadcast (nothing yet).
    ringRdc.chainRole = ChainRole::CHILD;
    EXPECT_TRUE(ringShootout.getLoopMembers().empty());
}

// Two rings that each closed and claimed a head, then got cabled together. The
// higher-MAC head must drop its own bracket, not just the anchor, or both sides
// keep running separate tournaments over one ring.
inline void mergedRingCoordinatorStandsDownToLowerMac(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> mine = {0x0A, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> rival = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setLoopMembersForTest({me, mine});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(mine.data());
    ASSERT_TRUE(suite->shootout->isCoordinator());
    ASSERT_EQ(suite->shootout->getBracket().size(), 2u);

    // A higher-MAC rival coordinator loses; ours survives untouched.
    std::array<uint8_t, 6> higher = {0xF0, 0, 0, 0, 0, 0};
    suite->shootout->onBracketReceived(higher.data(), {higher, me}, 4);
    EXPECT_TRUE(suite->shootout->isCoordinator());
    EXPECT_EQ(suite->shootout->getBracket().size(), 2u);

    // A lower-MAC one wins: we demote and adopt its bracket.
    suite->shootout->onBracketReceived(rival.data(), {rival, me, mine}, 5);
    EXPECT_FALSE(suite->shootout->isCoordinator());
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), rival.data(), 6), 0);
    EXPECT_EQ(suite->shootout->getBracket().size(), 3u);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 0u);
}

// BRACKET is a broadcast, so an unrelated ring's tournament reaches us. Before
// this guard the lower-MAC stand-down ran first and wiped a live tournament into
// a state nothing recovers: no coordinator, no bracket, phase still
// MATCH_IN_PROGRESS, and every restart path gated on isCoordinator().
inline void foreignRingBracketLeavesLiveTournamentIntact(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> mine = {0x0A, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setLoopMembersForTest({me, mine});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(mine.data());
    ASSERT_TRUE(suite->shootout->isCoordinator());
    ASSERT_EQ(suite->shootout->getBracket().size(), 2u);

    // A stranger ring, lower MAC than us, sharing no member with our bracket.
    std::array<uint8_t, 6> stranger = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> strangerPeer = {0x02, 0, 0, 0, 0, 0};
    suite->shootout->onBracketReceived(stranger.data(), {stranger, strangerPeer}, 7);

    EXPECT_TRUE(suite->shootout->isCoordinator());
    EXPECT_EQ(suite->shootout->getBracket().size(), 2u);
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), selfMac, 6), 0);

    // The harder case: a lower-MAC stranger whose bracket claims one of ours but
    // not us. Standing down to it drops our bracket and adopts nothing, which is
    // worse than ignoring it — there is no coordinator left to restart anything.
    suite->shootout->onBracketReceived(stranger.data(), {stranger, strangerPeer, mine}, 8);

    EXPECT_TRUE(suite->shootout->isCoordinator());
    EXPECT_EQ(suite->shootout->getBracket().size(), 2u);
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), selfMac, 6), 0);
}

// An abort from retry exhaustion drops the ring anchor with the cables still in
// place, and the RDC latch is edge-triggered so it never fires again. The
// coordinator has to notice the ring is still there and re-claim, or the whole
// ring waits for someone to unplug.
inline void abortedRingReclaimsWhileStillCabled(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    FakeRingRemoteDeviceCoordinator ringRdc;
    ringRdc.chainMembers = {{0x02, 0, 0, 0, 0, 0}};
    ShootoutManager ringShootout(&suite->player, suite->device.wirelessManager, &ringRdc);

    ringShootout.onRingClosed();
    ASSERT_TRUE(ringShootout.shouldEnterProposal());
    ASSERT_TRUE(ringShootout.isCoordinator());

    // Abort back to idle without touching a cable. The RDC latch stays set and is
    // edge-triggered, so it will never announce this ring again.
    ringShootout.resetToIdle();
    ASSERT_FALSE(ringShootout.isCoordinator());
    ASSERT_EQ(ringRdc.getChainRole(), ChainRole::RING);

    // Idle polls this predicate every tick and mounts the proposal on it. Both
    // halves have to work, so drive those rather than the manager's sync().
    ASSERT_TRUE(ringShootout.shouldEnterProposal());
    ringShootout.startProposal();

    EXPECT_TRUE(ringShootout.isCoordinator());
    EXPECT_EQ(ringShootout.getPhase(), ShootoutManager::Phase::PROPOSAL);
}

// Both heads of a merging pair latch and both announce. Adopting whichever frame
// lands last leaves A following B while B follows A, so nobody builds a bracket
// and every member parks in BracketReveal with the cables still in.
inline void mergedRingClaimantsSettleOnLowerMac(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> mine = {0x0A, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setLoopMembersForTest({me, mine});
    suite->shootout->onRingClosed();
    ASSERT_TRUE(suite->shootout->isCoordinator());

    // A higher-MAC rival head announces the merged ring; ours stands.
    std::array<uint8_t, 6> higher = {0xF0, 0, 0, 0, 0, 0};
    suite->shootout->onRingClosedReceived(higher.data(), {higher, me, mine});
    EXPECT_TRUE(suite->shootout->isCoordinator());

    // A lower-MAC one wins, and we follow it.
    std::array<uint8_t, 6> lower = {0x01, 0, 0, 0, 0, 0};
    suite->shootout->onRingClosedReceived(lower.data(), {lower, me, mine});
    EXPECT_FALSE(suite->shootout->isCoordinator());
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), lower.data(), 6), 0);
}

// The head's roster fills from announces that can still be in flight when the
// ring closes. Claiming on a one-entry roster must not run a solo tournament;
// the re-announce round picks the real members up and play proceeds.
inline void laggingRosterDoesNotRunSoloTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> peer = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    std::vector<uint8_t> lastRingClosed;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&lastRingClosed](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
                if (data[0] == static_cast<uint8_t>(ShootoutCmd::RING_CLOSED)) {
                    lastRingClosed.assign(data, data + len);
                }
                return 1;
            }));

    suite->shootout->setLoopMembersForTest({me});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_TRUE(suite->shootout->getBracket().empty());

    // The absent member's announce lands, and the next re-announce round names it
    // in the frame.
    suite->shootout->setLoopMembersForTest({me, peer});
    suite->fakeClock->advance(ShootoutManager::kConfirmRebroadcastMs + 100);
    suite->shootout->sync();
    ASSERT_EQ(lastRingClosed.size(), 3u + 6u * 2u);
    EXPECT_EQ(memcmp(&lastRingClosed[9], peer.data(), 6), 0);

    suite->shootout->onConfirmReceived(peer.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_EQ(suite->shootout->getBracket().size(), 2u);
}

// RING_CLOSED carries no ack, so a member that missed the frame would sit in
// idle forever while the coordinator blocks on its confirm. The coordinator
// re-announces on the same 1Hz cadence as CONFIRM until the roster is complete.
inline void ringClosedReannouncesWhileMembersUnconfirmed(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> peer = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    int ringClosedFrames = 0;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&ringClosedFrames](const uint8_t*, PktType, const uint8_t* data, const size_t) {
                if (data[0] == static_cast<uint8_t>(ShootoutCmd::RING_CLOSED)) ringClosedFrames++;
                return 1;
            }));

    suite->shootout->setLoopMembersForTest({me, peer});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    ASSERT_EQ(ringClosedFrames, 1);

    for (int i = 0; i < 30; i++) {
        suite->fakeClock->advance(100);
        suite->shootout->sync();
    }
    EXPECT_GT(ringClosedFrames, 1);

    // Once everyone has confirmed there is nobody left to reach.
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(peer.data());
    int afterConfirmed = ringClosedFrames;
    for (int i = 0; i < 30; i++) {
        suite->fakeClock->advance(100);
        suite->shootout->sync();
    }
    EXPECT_EQ(ringClosedFrames, afterConfirmed);
}

inline void bracketSizeAndByeMatchMemberCount(ShootoutManagerTests* suite) {
    auto runFor = [&](uint8_t memberCount, bool expectBye) {
        uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
        ON_CALL(*suite->device.mockPeerComms, getMacAddress())
            .WillByDefault(testing::Return(selfMac));
        ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
            .WillByDefault(testing::Return(1));
        std::vector<std::array<uint8_t, 6>> members;
        for (uint8_t i = 1; i <= memberCount; i++) members.push_back({i, 0, 0, 0, 0, 0});
        suite->shootout->setLoopMembersForTest(members);
        suite->shootout->resetToIdle();
        suite->shootout->onRingClosed();
        suite->shootout->startProposal();
        suite->shootout->confirmLocal();
        for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
        auto bracket = suite->shootout->getBracket();
        EXPECT_EQ(bracket.size(), memberCount);
        EXPECT_EQ(suite->shootout->hasBye(), expectBye);
    };
    runFor(/*memberCount=*/4, /*expectBye=*/false);
    runFor(/*memberCount=*/5, /*expectBye=*/true);
}

inline void receivingAllConfirmsAdvancesToBracketReveal(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    std::array<uint8_t, 6> m1 = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> m2 = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> m3 = {0x03, 0, 0, 0, 0, 0};
    suite->shootout->setLoopMembersForTest({m1, m2, m3});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(m2.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    suite->shootout->onConfirmReceived(m3.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
}

inline void coordinatorBroadcastsBracketOnAdvance(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}
    };
    suite->shootout->setLoopMembersForTest(members);

    // The fan-out is a single broadcast frame, not one unicast per member, and
    // it still owes an ack from each of the two peers.
    std::vector<std::array<uint8_t, 6>> destinations;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&destinations](const uint8_t* dst, PktType, const uint8_t*, const size_t) {
                std::array<uint8_t, 6> mac{};
                memcpy(mac.data(), dst, 6);
                destinations.push_back(mac);
                return 1;
            }));

    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    destinations.clear();  // drop the claim's own RING_CLOSED frame
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 2u);
    ASSERT_EQ(destinations.size(), 1u);
    EXPECT_EQ(memcmp(destinations[0].data(), MockDevice::BROADCAST_MAC, 6), 0);
}

inline void bracketAckClearsPendingForThatPeer(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}
    };
    suite->shootout->setLoopMembersForTest(members);
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    uint8_t seqId = suite->shootout->getLastBracketSeqId();
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 2u);
    suite->shootout->onBracketAckReceived(members[1].data(), seqId);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 1u);
    suite->shootout->onBracketAckReceived(members[2].data(), seqId);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 0u);
}

inline void bracketRetriesThreeTimesThenAborts(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}
    };
    suite->shootout->setLoopMembersForTest(members);
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    for (int i = 0; i < 60; i++) {
        suite->fakeClock->advance(100);
        suite->shootout->sync();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

inline void matchStartGatedOnAllBracketAcks(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01,0,0,0,0,0}, {0x02,0,0,0,0,0}, {0x03,0,0,0,0,0}, {0x04,0,0,0,0,0}
    };
    suite->shootout->setLoopMembersForTest(members);

    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();

    auto bracket = suite->shootout->getBracket();
    uint8_t bracketSeq = suite->shootout->getLastBracketSeqId();

    // Ack from only one peer. Reveal window expires; MATCH_START must NOT fire.
    for (const auto& m : bracket) {
        if (memcmp(m.data(), selfMac, 6) != 0) {
            suite->shootout->onBracketAckReceived(m.data(), bracketSeq);
            break;
        }
    }
    suite->fakeClock->advance(6000);
    suite->shootout->sync();
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // Ack the remaining peers. Now MATCH_START fires on next sync.
    for (const auto& m : bracket) {
        if (memcmp(m.data(), selfMac, 6) == 0) continue;
        suite->shootout->onBracketAckReceived(m.data(), bracketSeq);
    }
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), 0);
    auto pair = suite->shootout->getCurrentMatchPair();
    EXPECT_EQ(pair.first, bracket[0]);
    EXPECT_EQ(pair.second, bracket[1]);
}

inline void nonCoordinatorReceivingMatchStartIdentifiesRole(ShootoutManagerTests* suite) {
    // Receiver of MATCH_START decides duelist-vs-spectator from the carried
    // duelist pair. Cover both branches by running two MATCH_STARTs in the
    // same fixture: first names self (duelist), second names two peers
    // (spectator after the next match starts).
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setLoopMembersForTest({me, coord, other});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(other.data());
    suite->shootout->onBracketReceived(coord.data(), {me, coord, other}, 1);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);

    // Self in the duelist pair -> isLocalDuelist + opponentMac populated.
    suite->shootout->onMatchStartReceived(me.data(), coord.data(), 0, 2);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    EXPECT_TRUE(suite->shootout->isLocalDuelist());
    EXPECT_EQ(memcmp(suite->shootout->getOpponentMac().data(), coord.data(), 6), 0);

    // Different MATCH_START where self is NOT in the pair -> spectator.
    suite->shootout->onMatchStartReceived(coord.data(), other.data(), 1, 3);
    EXPECT_FALSE(suite->shootout->isLocalDuelist());
}

inline void winnerBroadcastsMatchResultAndAdvancesLocally(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({coord, me, opMac});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->onBracketReceived(coord.data(), {me, opMac, coord}, 1);
    suite->shootout->onMatchStartReceived(me.data(), opMac.data(), 0, 2);

    suite->shootout->reportLocalWin();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BETWEEN_MATCHES);
    EXPECT_TRUE(suite->shootout->isEliminated(opMac.data()));
    EXPECT_FALSE(suite->shootout->isEliminated(me.data()));
}

inline void matchResultReceivedAdvancesLocalBracket(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x04, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> aMac  = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> bMac  = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({coord, aMac, bMac, me});
    suite->shootout->startProposal();
    for (auto& m : std::vector<std::array<uint8_t,6>>{coord, aMac, bMac, me}) {
        suite->shootout->onConfirmReceived(m.data());
    }
    suite->shootout->onBracketReceived(coord.data(), {aMac, bMac, coord, me}, 1);
    suite->shootout->onMatchStartReceived(aMac.data(), bMac.data(), 0, 2);
    suite->shootout->onMatchResultReceived(aMac.data(), bMac.data(), 0, 3, aMac.data());
    EXPECT_TRUE(suite->shootout->isEliminated(bMac.data()));
    EXPECT_FALSE(suite->shootout->isEliminated(aMac.data()));
}

inline void drawWatchdogReplaysMatchStart(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    // Coordinator is self. Ack bracket from the peer.
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    suite->shootout->onBracketAckReceived(opMac.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->shootout->sync();  // fires MATCH_START 0
    uint8_t firstSeq = suite->shootout->getLastMatchStartSeqId();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    // Ack MATCH_START (peer present) so retry won't abort
    suite->shootout->onMatchStartAckReceived(opMac.data(), firstSeq);
    // No MATCH_RESULT for 11s — watchdog should re-broadcast MATCH_START.
    suite->fakeClock->advance(11000);
    suite->shootout->sync();
    uint8_t secondSeq = suite->shootout->getLastMatchStartSeqId();
    EXPECT_NE(firstSeq, secondSeq);
}

inline void peerLostCoordinatorAborts(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(other.data());
    suite->shootout->onBracketReceived(coord.data(), {me, other, coord}, 1);
    suite->shootout->onMatchStartReceived(me.data(), other.data(), 0, 2);
    suite->shootout->onPeerLostReceived(coord.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

inline void peerLostActiveDuelistAborts(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(other.data());
    suite->shootout->onBracketReceived(coord.data(), {me, other, coord}, 1);
    suite->shootout->onMatchStartReceived(me.data(), other.data(), 0, 2);
    suite->shootout->onPeerLostReceived(other.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

inline void peerLostSpectatorAborts(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> a = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({me, a, b, c});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : std::vector<std::array<uint8_t,6>>{me, a, b, c}) {
        suite->shootout->onConfirmReceived(m.data());
    }
    // bracket_ known to coordinator only via generateBracket — so construct a
    // known bracket by asking getBracket(). Since suite->shootout has generated
    // it during the confirm flow (self is coord), use it.
    // Drive coordinator through reveal → MATCH_START
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    for (const auto& m : suite->shootout->getBracket()) {
        if (memcmp(m.data(), me.data(), 6) != 0) {
            suite->shootout->onBracketAckReceived(m.data(), bSeq);
        }
    }
    suite->fakeClock->advance(6000);
    suite->shootout->sync();  // fires match 0

    // Figure out who's not dueling right now
    auto pair = suite->shootout->getCurrentMatchPair();
    // Pick any bracket member not in the current pair as the spectator to lose
    std::array<uint8_t, 6> spectator{};
    for (const auto& m : suite->shootout->getBracket()) {
        if (memcmp(m.data(), pair.first.data(), 6) != 0 &&
            memcmp(m.data(), pair.second.data(), 6) != 0 &&
            memcmp(m.data(), me.data(), 6) != 0) {
            spectator = m;
            break;
        }
    }
    suite->shootout->onPeerLostReceived(spectator.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

inline void finalMatchResultTriggersTournamentEnd(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    // Self is coord; bracket is already set. Ack bracket from peer.
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    suite->shootout->onBracketAckReceived(opMac.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->shootout->sync();  // fires MATCH_START 0
    uint8_t msSeq = suite->shootout->getLastMatchStartSeqId();
    suite->shootout->onMatchStartAckReceived(opMac.data(), msSeq);
    // Self wins.
    suite->shootout->reportLocalWin();
    // reportLocalWin → applyMatchResult → BETWEEN_MATCHES.
    // Coordinator sees no more pairs → broadcasts TOURNAMENT_END.
    // reportLocalWin calls maybeStartNextMatch which triggers end.
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    EXPECT_EQ(memcmp(suite->shootout->getTournamentWinner().data(), me.data(), 6), 0);
}

inline void startProposalClearsAllPriorTournamentState(ShootoutManagerTests* suite) {
    // Run tournament 1 to ENDED, then call startProposal again and verify
    // that no state from tournament 1 leaks into tournament 2.
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    // Tournament 1
    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    suite->shootout->onBracketAckReceived(opMac.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->shootout->sync();
    uint8_t msSeq = suite->shootout->getLastMatchStartSeqId();
    suite->shootout->onMatchStartAckReceived(opMac.data(), msSeq);
    suite->shootout->reportLocalWin();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    ASSERT_FALSE(suite->shootout->getBracket().empty());
    ASSERT_TRUE(suite->shootout->isEliminated(opMac.data()));
    ASSERT_GE(suite->shootout->getCurrentMatchIndex(), 0);

    // Tournament 2 — previously leaked bracket_, eliminated_, currentMatchIndex_.
    suite->shootout->startProposal();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_TRUE(suite->shootout->getBracket().empty());
    EXPECT_EQ(suite->shootout->getConfirmedCount(), 0u);
    EXPECT_FALSE(suite->shootout->isEliminated(opMac.data()));
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), -1);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 0u);
}

inline void confirmRecordsPeerName(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    suite->shootout->setLoopMembersForTest({
        {0x01, 0, 0, 0, 0, 0},
        {0x02, 0, 0, 0, 0, 0},
    });
    suite->shootout->startProposal();
    uint8_t peerMac[6] = {0x02, 0, 0, 0, 0, 0};
    const char* peerName = "alice";
    suite->shootout->onConfirmReceived(peerMac, peerName);
    EXPECT_EQ(suite->shootout->getNameForMac(peerMac), "alice");
    // Unknown MAC falls back to hex suffix.
    uint8_t other[6] = {0xAA, 0, 0, 0, 0, 0xBE};
    EXPECT_EQ(suite->shootout->getNameForMac(other), "BE");
}

inline void duplicateMatchResultDoesNotDoubleAdvance(ShootoutManagerTests* suite) {
    // Reproduces the hardware bug where ESP-NOW link-layer duplicate delivery
    // of MATCH_RESULT made the coordinator call maybeStartNextMatch twice per
    // match. Second call incremented currentMatchIndex_ and triggered a
    // premature advance-round, collapsing the bracket from 4 to 3.
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me  = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b   = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c   = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> d   = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({me, b, c, d});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : {me, b, c, d}) suite->shootout->onConfirmReceived(m.data());
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    for (auto& m : {b, c, d}) suite->shootout->onBracketAckReceived(m.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->shootout->sync();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    ASSERT_EQ(suite->shootout->getBracket().size(), 4u);
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), 0);

    // This test exercises the non-coord duplicate path: coord observes a
    // MATCH_RESULT twice where neither winner nor loser is the coord itself.
    // With the coord in match 0, reportLocalWin (not onMatchResultReceived)
    // would be the real-world entry point, so the dedup path wouldn't fire.
    // Under the current seed (fakeClock=1000 XOR selfMac=01 -> 1001), mt19937
    // shuffles {01,02,03,04} to {04,03,01,02}, so match 0 = {04,03}. Guard
    // against silent seed drift — if this fires, fix the MACs above so match
    // 0 excludes the coordinator rather than masking the regression.
    auto pair = suite->shootout->getCurrentMatchPair();
    ASSERT_NE(memcmp(pair.first.data(), me.data(), 6), 0)
        << "coordinator unexpectedly in match 0 — shuffle seed drifted";
    ASSERT_NE(memcmp(pair.second.data(), me.data(), 6), 0)
        << "coordinator unexpectedly in match 0 — shuffle seed drifted";

    // Whichever two devices are in match 0 — deliver MATCH_RESULT for them
    // TWICE (same winner + loser). Simulates ESP-NOW duplicate delivery.
    const uint8_t* winner = pair.first.data();
    const uint8_t* loser  = pair.second.data();
    suite->shootout->onMatchResultReceived(winner, loser, 0, /*seqId=*/7, winner);
    suite->shootout->onMatchResultReceived(winner, loser, 0, /*seqId=*/7, winner);

    // After one real match: bracket is still 4, currentMatchIndex is 1
    // (coord moved on to match 1). The duplicate must NOT have advanced.
    EXPECT_EQ(suite->shootout->getBracket().size(), 4u);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), 1);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
}

inline void tournamentEndRetriesUntilAcked(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    // Count sendData invocations so we can observe that sync() re-sends
    // TOURNAMENT_END when the pending-ack timer expires. Regression guard:
    // if sendTournamentEndToPeers is called once and never retried, the second
    // send count below stays equal to the first and this test fails.
    std::atomic<int> sendCount{0};
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&sendCount](const uint8_t*, PktType, const uint8_t*, const size_t) {
                sendCount.fetch_add(1);
                return 1;
            }));

    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    suite->shootout->onBracketAckReceived(opMac.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->shootout->sync();
    uint8_t msSeq = suite->shootout->getLastMatchStartSeqId();
    suite->shootout->onMatchStartAckReceived(opMac.data(), msSeq);
    suite->shootout->reportLocalWin();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    ASSERT_EQ(suite->shootout->getTournamentEndPendingAckCount(), 1u);

    // Snapshot send count immediately after the initial TOURNAMENT_END
    // broadcast — anything further must come from the retry path.
    int sendCountAfterInitialBroadcast = sendCount.load();

    // Advance past the first retry interval (ackTimeoutForRetry(0)=100ms) and
    // drive sync(). A retry must re-broadcast TOURNAMENT_END.
    suite->fakeClock->advance(200);
    suite->shootout->sync();
    EXPECT_GT(sendCount.load(), sendCountAfterInitialBroadcast);
    EXPECT_EQ(suite->shootout->getTournamentEndPendingAckCount(), 1u);

    // Correct ack clears the pending entry.
    uint8_t teSeq = suite->shootout->getLastTournamentEndSeqId();
    suite->shootout->onTournamentEndAckReceived(opMac.data(), teSeq);
    EXPECT_EQ(suite->shootout->getTournamentEndPendingAckCount(), 0u);
}

inline void matchResultRetriesUntilAcked(ShootoutManagerTests* suite) {
    // Self is coord; match 0 is self vs opMac. reportLocalWin() broadcasts
    // MATCH_RESULT to every confirmed peer (including coord, but
    // sendReliablyToPeers skips self). Without retry, a single dropped packet
    // would strand a peer whose state never advances.
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> spec  = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    std::atomic<int> sendCount{0};
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&sendCount](const uint8_t*, PktType, const uint8_t*, const size_t) {
                sendCount.fetch_add(1);
                return 1;
            }));

    suite->shootout->setLoopMembersForTest({me, opMac, spec});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->onConfirmReceived(spec.data());
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    suite->shootout->onBracketAckReceived(opMac.data(), bSeq);
    suite->shootout->onBracketAckReceived(spec.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->shootout->sync();
    uint8_t msSeq = suite->shootout->getLastMatchStartSeqId();
    suite->shootout->onMatchStartAckReceived(opMac.data(), msSeq);
    suite->shootout->onMatchStartAckReceived(spec.data(), msSeq);

    // Self wins match 0 → broadcasts MATCH_RESULT to opMac and spec.
    suite->shootout->reportLocalWin();
    EXPECT_EQ(suite->shootout->getMatchResultPendingAckCount(), 2u);

    int sendCountAfterInitial = sendCount.load();

    // After ackTimeoutForRetry(0)=100ms, sync() retries to both pending peers.
    suite->fakeClock->advance(200);
    suite->shootout->sync();
    EXPECT_GT(sendCount.load(), sendCountAfterInitial);
    EXPECT_EQ(suite->shootout->getMatchResultPendingAckCount(), 2u);

    // Acks clear pending.
    suite->shootout->onMatchResultAckReceived(opMac.data(), suite->shootout->getLastMatchResultSeqId());
    EXPECT_EQ(suite->shootout->getMatchResultPendingAckCount(), 1u);
    suite->shootout->onMatchResultAckReceived(spec.data(), suite->shootout->getLastMatchResultSeqId());
    EXPECT_EQ(suite->shootout->getMatchResultPendingAckCount(), 0u);
}

inline void isHunterRestoredAfterTournament(ShootoutManagerTests* suite) {
    // primeMatchManagerForMatch overrides player_->isHunter() based on MAC
    // ordering during Shootout. resetToIdle must restore the pre-tournament
    // snapshot captured in startProposal — both as a direct call (clean exit)
    // and via the abort path (ShootoutAborted::onStateDismounted → resetToIdle).
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};  // < self → forces flip
    std::array<uint8_t, 6> third = {0x07, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    bool originalIsHunter = suite->player.isHunter();

    // Path 1: direct resetToIdle. The unit-test fixture doesn't wire a
    // MatchManager (primeMatchManagerForMatch's nullptr early-return skips
    // setIsHunter), so we mutate directly to exercise the restore.
    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->startProposal();
    suite->player.setIsHunter(!originalIsHunter);
    suite->shootout->resetToIdle();
    EXPECT_EQ(suite->player.isHunter(), originalIsHunter);

    // Path 2: via abort (coord lost during a match). ShootoutAborted's
    // onStateDismounted calls resetToIdle on real hardware.
    suite->shootout->setLoopMembersForTest({me, opMac, third});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->onConfirmReceived(third.data());
    suite->shootout->onBracketReceived(opMac.data(), {me, opMac, third}, 1);
    suite->shootout->onMatchStartReceived(me.data(), opMac.data(), 0, 2);
    suite->player.setIsHunter(!originalIsHunter);
    suite->shootout->onPeerLostReceived(opMac.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    suite->shootout->resetToIdle();
    EXPECT_EQ(suite->player.isHunter(), originalIsHunter);
}

// onLocalRDCDisconnect is idempotent: when the same MAC is reported lost twice
// (e.g. RDC fires peerLostCallback AND chainChangeCallback for the same drop),
// the second call must not broadcast a duplicate PEER_LOST.
inline void localRDCDisconnectIsIdempotent(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> a     = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b     = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c     = {0x04, 0, 0, 0, 0, 0};  // will drop
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    suite->shootout->setLoopMembersForTest({me, a, b, c});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : std::vector<std::array<uint8_t,6>>{me, a, b, c}) {
        suite->shootout->onConfirmReceived(m.data());
    }
    uint8_t bSeq = suite->shootout->getLastBracketSeqId();
    for (const auto& m : suite->shootout->getBracket()) {
        if (memcmp(m.data(), me.data(), 6) != 0) {
            suite->shootout->onBracketAckReceived(m.data(), bSeq);
        }
    }
    suite->fakeClock->advance(6000);
    suite->shootout->sync();  // start match 0

    // Pick a non-duelist for the drop.
    auto pair = suite->shootout->getCurrentMatchPair();
    std::array<uint8_t, 6> dropping{};
    bool found = false;
    for (const auto& m : suite->shootout->getBracket()) {
        if (memcmp(m.data(), pair.first.data(), 6) != 0 &&
            memcmp(m.data(), pair.second.data(), 6) != 0 &&
            memcmp(m.data(), me.data(), 6) != 0) {
            dropping = m;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    std::atomic<int> sendCount{0};
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, testing::_, testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly([&sendCount](const uint8_t*, PktType, const uint8_t*, const size_t) {
            sendCount++; return 1;
        });

    suite->shootout->onLocalRDCDisconnect(dropping.data());
    int afterFirst = sendCount.load();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    EXPECT_GT(afterFirst, 0);

    suite->shootout->onLocalRDCDisconnect(dropping.data());
    EXPECT_EQ(sendCount.load(), afterFirst) << "duplicate disconnect should not re-broadcast";
}

// ShootoutProposal must NOT exit to Idle on a single-tick !isLoop() blip —
// cable nudges flicker isLoop() for one loop iteration, and the original code
// wiped tournament state on every tick that read false. Debounce requires the
// loss to persist for LOOP_BREAK_DEBOUNCE_MS before treating it as a real break.
inline void shootoutProposalDebouncesTransientLoopBreak(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    FakeChainDuelManager fakeCdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    fakeCdm.setIsLoop(true);
    suite->shootout->setLoopMembersForTest({{0x01,0,0,0,0,0}, {0x02,0,0,0,0,0}});
    suite->shootout->startProposal();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);

    GameContext ctx;
    ctx.shootoutManager = suite->shootout;
    ctx.chainDuelManager = &fakeCdm;
    ShootoutProposal state(ctx);

    // Single-tick blip: phase must remain PROPOSAL.
    fakeCdm.setIsLoop(false);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_FALSE(state.transitionToAborted());

    // Loop returns within debounce window — debounce cleared, phase intact.
    fakeCdm.setIsLoop(true);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_FALSE(state.transitionToAborted());

    // Persistent loss past the debounce window sets Phase::ABORTED and fires
    // transitionToAborted.
    fakeCdm.setIsLoop(false);
    state.onStateLoop(nullptr);  // start debounce
    suite->fakeClock->advance(2000);  // well past any reasonable debounce window
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    EXPECT_TRUE(state.transitionToAborted());
}

// Same debounce contract on ShootoutBracketReveal (tournament state is more
// expensive to wipe here — bracket and pendingAcks vanish too).
inline void shootoutBracketRevealDebouncesTransientLoopBreak(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    FakeChainDuelManager fakeCdm(&suite->player, suite->device.wirelessManager, &suite->rdc);
    fakeCdm.setIsLoop(true);
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01,0,0,0,0,0}, {0x02,0,0,0,0,0}, {0x03,0,0,0,0,0}
    };
    suite->shootout->setLoopMembersForTest(members);
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);

    GameContext ctx;
    ctx.shootoutManager = suite->shootout;
    ctx.chainDuelManager = &fakeCdm;
    ShootoutBracketReveal state(ctx);

    fakeCdm.setIsLoop(false);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_FALSE(state.transitionToAborted());

    fakeCdm.setIsLoop(true);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_FALSE(state.transitionToAborted());

    fakeCdm.setIsLoop(false);
    state.onStateLoop(nullptr);
    suite->fakeClock->advance(2000);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    EXPECT_TRUE(state.transitionToAborted());
}

// The ESP-NOW peer table holds 20 entries, so a unicast fan-out cannot address a
// larger ring at all. One broadcast frame reaches every member regardless of
// ring size, while each member still owes its own unicast ack.
inline void bracketFanOutIsOneFrameBeyondPeerTable(ShootoutManagerTests* suite) {
    constexpr size_t RING_SIZE = 30;
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    std::vector<std::array<uint8_t, 6>> members;
    for (size_t i = 0; i < RING_SIZE; i++) {
        members.push_back({static_cast<uint8_t>(0x01 + i), 0, 0, 0, 0, 0});
    }
    suite->shootout->setLoopMembersForTest(members);

    std::vector<std::array<uint8_t, 6>> destinations;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&destinations](const uint8_t* dst, PktType, const uint8_t*, const size_t) {
                std::array<uint8_t, 6> mac{};
                memcpy(mac.data(), dst, 6);
                destinations.push_back(mac);
                return 1;
            }));

    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    destinations.clear();  // drop the claim's own RING_CLOSED frame
    for (auto& m : members)
        suite->shootout->onConfirmReceived(m.data());

    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_EQ(suite->shootout->getBracket().size(), RING_SIZE);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), RING_SIZE - 1);
    ASSERT_EQ(destinations.size(), 1u);
    EXPECT_EQ(memcmp(destinations[0].data(), MockDevice::BROADCAST_MAC, 6), 0);
}

// A retry round is one frame for every member still owing an ack, not one frame
// each: per-peer retries would spend a peer-table slot per member.
inline void bracketRetryIsOneFramePerRound(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}, {0x04, 0, 0, 0, 0, 0}};
    suite->shootout->setLoopMembersForTest(members);
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members)
        suite->shootout->onConfirmReceived(m.data());
    ASSERT_EQ(suite->shootout->getBracketPendingAckCount(), 3u);

    int sends = 0;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&sends](const uint8_t*, PktType, const uint8_t*, const size_t) {
                sends++;
                return 1;
            }));

    // First backoff is ackTimeoutForRetry(0) = 100ms and every pending entry was
    // armed together, so one round covers all three.
    suite->fakeClock->advance(150);
    suite->shootout->sync();
    EXPECT_EQ(sends, 1);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 3u);
}

// Rings share the radio channel, so a broadcast bracket lands on devices that
// are not in it. A device absent from the roster must neither adopt it nor ack
// it — an ack would enrol it in a neighbouring ring's tournament.
inline void foreignBracketIsNeitherAdoptedNorAcked(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x0A, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({{0x0A, 0, 0, 0, 0, 0}, {0x0B, 0, 0, 0, 0, 0}});
    suite->shootout->startProposal();

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kShootoutCommandAck, testing::_, testing::_))
        .Times(0);

    std::array<uint8_t, 6> alienCoord = {0x01, 0, 0, 0, 0, 0};
    suite->shootout->onBracketReceived(alienCoord.data(),
                                       {alienCoord,
                                        {0x02, 0, 0, 0, 0, 0},
                                        {0x03, 0, 0, 0, 0, 0}},
                                       1);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_TRUE(suite->shootout->getBracket().empty());
}

// Every other shootout command is broadcast too, so each must ignore a sender or
// a named MAC from outside this device's ring. Without this a neighbouring ring
// could start matches in, or abort, a tournament it has no part in.
inline void strayRingCommandsLeaveTournamentUntouched(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> alienA = {0xA1, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> alienB = {0xA2, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setLoopMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    for (auto& m : {coord, me, other})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(coord.data(), {me, other, coord}, 1);
    suite->shootout->onMatchStartReceived(me.data(), other.data(), 0, 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), 0);

    suite->shootout->onMatchStartReceived(alienA.data(), alienB.data(), 5, 3);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), 0);
    EXPECT_TRUE(suite->shootout->isLocalDuelist());

    suite->shootout->onMatchResultReceived(alienA.data(), alienB.data(), 5, 4, alienA.data());
    EXPECT_FALSE(suite->shootout->isEliminated(alienB.data()));
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    suite->shootout->onPeerLostReceived(alienA.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    suite->shootout->onTournamentEndReceived(alienA.data(), 5);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    suite->shootout->onAbortReceived(alienA.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // A ring member's ABORT still lands.
    suite->shootout->onAbortReceived(coord.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}
