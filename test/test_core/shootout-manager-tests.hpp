#pragma once

#include <gtest/gtest.h>
#include <map>
#include <utility>
#include <vector>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "rdc-test-harness.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/shootout-packets.hpp"
#include "game/shootout-manager.hpp"
#include "game/match-manager.hpp"
#include "game/posse-manager.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-apps.hpp"
#include "game/player.hpp"

class ShootoutManagerTests : public RdcTestHarness {
public:
    /** The shootout cases are written against a device whose MAC sorts lowest. */
    ShootoutManagerTests() {
        const uint8_t lowestMac[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
        memcpy(localMac, lowestMac, 6);
    }

    /** The harness's device with a ShootoutManager on its RDC and the air recorded. */
    void SetUp() override {
        RdcTestHarness::SetUp();
        // What goes on the air, so a test learns a frame's seqId the way a peer does.
        ON_CALL(*device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(
        testing::DoAll(testing::Invoke([this](const uint8_t*, PktType type, const uint8_t* data,
                                              const size_t len) { recordFrame(type, data, len); }),
                       testing::Return(1)));
        rebuildShootout(nullptr);
    }

    /**
     * Replaces `shootout` with a fresh one on the fixture's RDC, built on
     * `matchManager`, which must outlive it (pass nullptr to drop a test-local
     * one).
     */
    void rebuildShootout(MatchManager* matchManager) {
        // Old manager first: built while it stands, the new one is refused the
        // shootout packet types.
        delete shootout;
        shootout = new ShootoutManager(&player, device.wirelessManager, &rdc, matchManager);
    }

    /**
     * Head a chain out of OUTPUT, then take our own MAC back on INPUT — the only
     * local evidence that a ring closed. A tournament only exists on a closed
     * ring, and sync()'s ring-break guard reads that directly, so any case that
     * drives a tournament forward has to stand one up.
     */
    void closeRingOnJacks() {
        const uint8_t upstream[6] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
        connectJackTo(outJack, peerMac);
        connectJackTo(inJack, upstream, localMac);
    }

    /**
     * Lets both jacks fall silent, so the RDC declares the links lost on its
     * next sync and the latch opens. A single device cannot be argued out of a
     * ring any other way, and this is what a pulled cable looks like from here.
     * Costs HELLO_SILENT_LINK_MS of clock — a fifth of the abort debounce.
     */
    void openRingOnJacks() {
        fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
        rdc.sync(&device);
    }

    /**
     * Ring closed, everyone confirmed, bracket acked, first match started. The
     * ramp almost every tournament case needs before it can say anything.
     * `members` must start with this device's own MAC.
     */
    void driveToFirstMatch(const std::vector<std::array<uint8_t, 6>>& members) {
        closeRingOnJacks();
        shootout->setRingMembersForTest(members);
        shootout->onRingClosed();
        shootout->startProposal();
        for (const std::array<uint8_t, 6>& m : members)
            shootout->onConfirmReceived(m.data());
        const uint8_t bracketSeq = lastSeqId(PktType::kShootoutBracket);
        for (size_t i = 1; i < members.size(); ++i) {
            ack(PktType::kShootoutBracket, members[i].data(), bracketSeq);
        }
        fakeClock->advance(6000);
        tick();
    }

    /** One device loop pass: the transport pumps its retries, then the manager runs. */
    void tick() {
        device.wirelessManager->sync();
        shootout->sync();
    }

    /** Steps fakeClock and tick() in 20ms increments so no 100ms retry backoff window is skipped.
     */
    void runLoopFor(unsigned long ms) {
        for (unsigned long elapsed = 0; elapsed < ms; elapsed += 20) {
            fakeClock->advance(20);
            tick();
        }
    }

    /**
     * Appends the seqId of every MATCH_RESULT frame sent from here on. A re-send
     * after abandonment goes out under a fresh id; a retransmit repeats the last.
     */
    void recordMatchResultSeqIds(std::vector<uint8_t>* seqIds) {
        ON_CALL(*device.mockPeerComms,
                sendData(testing::_, PktType::kShootoutMatchResult, testing::_, testing::_))
        .WillByDefault(
        testing::Invoke([seqIds](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
            if (len == sizeof(ShootoutMatchResultPayload)) seqIds->push_back(data[0]);
            return 1;
        }));
    }

    /**
     * Delivers `fromMac`'s ack of the `type` frame carrying `seqId` through the
     * radio receive path, the way a peer's kReliableAck arrives.
     */
    void ack(PktType type, const uint8_t* fromMac, uint8_t seqId) {
        ReliableAckPayload ackPayload{type, seqId};
        device.wirelessManager->getReliableTransport()->deliverIncoming(
        PktType::kReliableAck, fromMac, reinterpret_cast<const uint8_t*>(&ackPayload),
        sizeof(ackPayload));
    }

    /**
     * Notes one outgoing frame. Tests that install their own sendData action
     * call this from it so lastSeqId() keeps seeing the air.
     */
    void recordFrame(PktType type, const uint8_t* data, size_t len) {
        if (len == 0) return;
        lastSeqIdByType[type] = data[0];
    }

    /** seqId of the last `type` frame this device put on the air (0 if none). */
    uint8_t lastSeqId(PktType type) const {
        std::map<PktType, uint8_t>::const_iterator it = lastSeqIdByType.find(type);
        return it == lastSeqIdByType.end() ? 0 : it->second;
    }

    /** Unacked peers still owed the `type` frame carrying `seqId`. */
    size_t pending(PktType type, uint8_t seqId) const {
        return device.wirelessManager->getReliableTransport()->pendingCount(type, seqId);
    }

    /** Connects both jacks as a relayed ring member under `head` (defaults to 0x06). */
    void joinRelayedRing(const uint8_t* head = nullptr) {
        const uint8_t downstream[6] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
        const uint8_t upstream[6] = {0x05, 0x00, 0x00, 0x00, 0x00, 0x00};
        const uint8_t defaultHead[6] = {0x06, 0x00, 0x00, 0x00, 0x00, 0x00};
        connectJackTo(outJack, downstream);
        connectJackTo(inJack, upstream, head != nullptr ? head : defaultHead, /*isHunter=*/false,
                      HELLO_FLAG_RING_CLOSED);
    }

    /**
     * Rebinds `shootout` to `memberRdc` as a relayed ring child under `head`. It
     * primes `matchManager` on each MATCH_START when the test brings one.
     */
    void followRingHead(const std::array<uint8_t, 6>& head, MatchManager* matchManager = nullptr) {
        memberRdc.chainRole = ChainRole::CHILD;
        memberRdc.headMac = head;
        delete shootout;
        shootout = new ShootoutManager(&player, device.wirelessManager, &memberRdc, matchManager);
    }

    /** Rebinds `shootout` to `memberRdc` as a latched ring head serving `served`. */
    void headRingServing(const std::vector<std::array<uint8_t, 6>>& served) {
        memberRdc.chainRole = ChainRole::RING;
        memberRdc.chainMembers = served;
        delete shootout;
        shootout = new ShootoutManager(&player, device.wirelessManager, &memberRdc, nullptr);
    }

    /** Both cables go quiet: the links lapse and the ring is provably open. */
    void loseTheRing() {
        fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
        rdc.sync(&device);
    }

    /** Frees the manager while the harness's RDC it subscribed to still stands. */
    void TearDown() override {
        delete shootout;
        shootout = nullptr;
        RdcTestHarness::TearDown();
    }

    FakeRingRemoteDeviceCoordinator memberRdc;
    Player player{"TEST", Allegiance::RESISTANCE, true};
    ShootoutManager* shootout = nullptr;
    std::map<PktType, uint8_t> lastSeqIdByType;
    uint8_t peerMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
};

/** Matches the payload of a kReliableAck frame that answers a `type` frame. */
inline testing::Matcher<const uint8_t*> ackFor(PktType type) {
    return testing::Truly([type](const uint8_t* data) {
        ReliableAckPayload ackPayload;
        memcpy(&ackPayload, data, sizeof(ackPayload));
        return ackPayload.ackedType == type;
    });
}

inline void localConfirmIsRecordedAndBroadcast(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    suite->shootout->setRingMembersForTest({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});
    suite->shootout->startProposal();
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kShootoutConfirm, testing::_, testing::_))
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
    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, testing::_, testing::_, testing::_))
    .Times(testing::AnyNumber())
    .WillRepeatedly(testing::Return(1));
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();

    for (int i = 0; i < 30; i++) {
        suite->fakeClock->advance(100);
        suite->tick();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
}

inline void coordinatorIsTheRingHeadThatDrew(ShootoutManagerTests* suite) {
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    std::array<uint8_t, 6> a = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c = {0x03, 0, 0, 0, 0, 0};

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({a, b, c});
    suite->shootout->startProposal();
    EXPECT_FALSE(suite->shootout->isCoordinator()) << "no bracket drawn yet";
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(a.data());
    suite->shootout->onConfirmReceived(c.data());

    EXPECT_TRUE(suite->shootout->isCoordinator());
    EXPECT_EQ(suite->shootout->getCoordinatorMac(), b);
    EXPECT_EQ(suite->shootout->getBracket().size(), 3u);
}

inline void ringClosedAnnouncesRosterToMembers(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}};
    suite->shootout->setRingMembersForTest(members);

    std::vector<uint8_t> frame;
    std::array<uint8_t, 6> destination{};
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutRingClosed, testing::_, testing::_))
    .WillByDefault(testing::Invoke(
    [&frame, &destination](const uint8_t* dst, PktType, const uint8_t* data, const size_t len) {
        memcpy(destination.data(), dst, 6);
        frame.assign(data, data + len);
        return 1;
    }));

    EXPECT_FALSE(suite->shootout->shouldEnterProposal());

    // Head a chain out of OUTPUT, then take our own MAC back on INPUT: the RDC
    // latches the ring on that and calls the manager back.
    const uint8_t upstream[6] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
    suite->connectJackTo(suite->outJack, suite->peerMac);
    suite->connectJackTo(suite->inJack, upstream, suite->localMac);
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);
    EXPECT_TRUE(suite->shootout->shouldEnterProposal());

    EXPECT_EQ(memcmp(destination.data(), MockDevice::BROADCAST_MAC, 6), 0);
    ASSERT_EQ(frame.size(), 2 + members.size() * 6) << "the unused roster tail went on the air";
    ShootoutRosterPayload roster{};
    memcpy(&roster, frame.data(), frame.size());
    EXPECT_EQ(roster.count, members.size());
    for (size_t i = 0; i < members.size(); i++) {
        EXPECT_EQ(memcmp(roster.macs[i], members[i].data(), 6), 0) << "member " << i;
    }
}

// Ring closure reaches the manager through the coordinator, not a hand call.
// Every other case here invokes onRingClosed() directly, so none of them notices
// if the manager stops subscribing and the tournament simply never starts.
inline void ringClosureFromCoordinatorAnnouncesRing(ShootoutManagerTests* suite) {
    suite->shootout->setRingMembersForTest({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});

    // Assert the RING_CLOSED broadcast rather than shouldEnterProposal(), which
    // reads true from the RDC latch alone.
    std::vector<uint8_t> frame;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutRingClosed, testing::_, testing::_))
    .WillByDefault(
    testing::Invoke([&frame](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        frame.assign(data, data + len);
        return 1;
    }));

    // Head a chain out of OUTPUT, then take our own MAC back on INPUT — the only
    // local evidence that a ring closed.
    const uint8_t upstream[6] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
    suite->connectJackTo(suite->outJack, suite->peerMac);
    suite->connectJackTo(suite->inJack, upstream, suite->localMac);
    ASSERT_TRUE(suite->rdc.isInRing());

    // The claim only; entering PROPOSAL is the app's transition, driven off
    // shouldEnterProposal() on a later tick.
    EXPECT_FALSE(frame.empty()) << "ring closure never went out on kShootoutRingClosed";
}

// A member is never the head that latches the ring, so it learns closure twice
// over: the relayed HELLO flag its proposal gate polls, and the coordinator's
// roster broadcast. A roster it is absent from belongs to a neighbouring ring.
inline void ringClosedBroadcastPromotesOnlyItsOwnMembers(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->joinRelayedRing();
    ASSERT_TRUE(suite->rdc.isInRing());

    std::array<uint8_t, 6> alienCoord = {0xA1, 0, 0, 0, 0, 0};
    suite->shootout->onRingClosedReceived(alienCoord.data(), {alienCoord, other});
    EXPECT_FALSE(suite->shootout->shouldEnterProposal());
    EXPECT_TRUE(suite->shootout->getRingMembers().empty());

    suite->shootout->onRingClosedReceived(coord.data(), {coord, me, other});
    EXPECT_TRUE(suite->shootout->shouldEnterProposal());
    EXPECT_EQ(suite->shootout->getRingMembers().size(), 3u);

    // The member's own CONFIRM now has a ring to reach; an empty roster would
    // have suppressed it at the broadcast guard.
    suite->shootout->startProposal();
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kShootoutConfirm, testing::_, testing::_))
    .Times(testing::AtLeast(1))
    .WillRepeatedly(testing::Return(1));
    suite->shootout->confirmLocal();
}

// A member takes the roster and then loses the ring before the app polls it into
// proposal. Nothing clears that roster while the phase is IDLE, so a copy of a
// ring that is now unplugged is still sitting there, and only live membership
// can tell it apart from a ring that is really closed.
inline void openRingRefusesProposalDespiteLatchedRoster(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    suite->joinRelayedRing();
    suite->shootout->onRingClosedReceived(coord.data(), {coord, me, other});
    ASSERT_TRUE(suite->shootout->shouldEnterProposal());
    ASSERT_EQ(suite->shootout->getRingMembers().size(), 3u)
        << "the roster never arrived; nothing below would be testing anything";

    suite->loseTheRing();
    ASSERT_FALSE(suite->rdc.isInRing());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::IDLE);

    // The roster is untouched, so the refusal can only be coming from the live
    // membership half of the gate.
    EXPECT_EQ(suite->shootout->getRingMembers().size(), 3u);
    EXPECT_FALSE(suite->shootout->shouldEnterProposal());
}

// The coordinator's roster is the RDC's head-only chain-member set plus itself:
// getChainMembers() enumerates who announced to the head, never the head.
inline void ringHeadMembersComeFromRdcRoster(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->headRingServing({{0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}});

    std::vector<std::array<uint8_t, 6>> members = suite->shootout->getRingMembers();
    ASSERT_EQ(members.size(), 3u);
    EXPECT_EQ(memcmp(members[2].data(), selfMac, 6), 0);

    // A device the RDC does not treat as the ring's roster authority has nothing
    // to read and falls back to whatever the coordinator broadcast (nothing yet).
    suite->memberRdc.chainRole = ChainRole::CHILD;
    EXPECT_TRUE(suite->shootout->getRingMembers().empty());
}

inline void foreignRingBracketLeavesLiveTournamentIntact(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x09, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> mine = {0x0A, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me, mine});
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

inline void onlyTheRingHeadDrawsTheBracket(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> peer = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setRingMembersForTest({me, peer});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(peer.data());

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_TRUE(suite->shootout->getBracket().empty())
        << "a device that does not head the ring drew a bracket";
}

inline void memberJoinsOnlyItsRingHeadsBracket(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> head = {0x06, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->joinRelayedRing();
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);
    ASSERT_EQ(memcmp(suite->rdc.getHeadMac(), head.data(), 6), 0);
    suite->shootout->setRingMembersForTest({me, head, other});
    suite->shootout->startProposal();

    suite->shootout->onBracketReceived(other.data(), {other, me, head}, 1);
    EXPECT_TRUE(suite->shootout->getBracket().empty())
        << "a member joined a bracket its ring head did not draw";

    suite->shootout->onBracketReceived(head.data(), {head, me, other}, 1);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_EQ(suite->shootout->getCoordinatorMac(), head);
}

inline void headKeepsWaitingOnAMemberPrunedFromTheRoster(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> peer = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> pruned = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->headRingServing({peer});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->memberRdc.chainMembers = {peer, pruned};
    suite->shootout->sync();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(peer.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);

    suite->memberRdc.chainMembers = {peer};
    suite->shootout->sync();

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "a member pruned from the roster for a moment lost its seat in the bracket";
}

inline void deviceThatComesToHeadTheRingDrawsTheBracket(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> peer = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setRingMembersForTest({me, peer});
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(peer.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    ASSERT_TRUE(suite->shootout->getBracket().empty());

    suite->closeRingOnJacks();
    suite->shootout->sync();

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_EQ(suite->shootout->getBracket().size(), 2u)
        << "the device heading the ring never drew the bracket everyone waits on";
    EXPECT_EQ(suite->shootout->getCoordinatorMac(), me);
}

inline void deviceWaitingOnABracketStillCountsConfirms(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> head = {0x06, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> late = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(head);
    suite->shootout->onRingClosedReceived(head.data(), {me, head});
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(head.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);

    suite->shootout->onRingClosedReceived(head.data(), {me, head, late});
    suite->shootout->onConfirmReceived(late.data());

    EXPECT_TRUE(suite->shootout->hasConfirmed(late.data()))
        << "a confirm reaching a device that may have to draw was dropped";
}

inline void memberKeepsConfirmingUntilItHoldsABracket(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> head = {0x06, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(head);
    suite->shootout->setRingMembersForTest({me, head});
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(head.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kShootoutConfirm, testing::_, testing::_))
    .Times(testing::AtLeast(1))
    .WillRepeatedly(testing::Return(1));

    suite->fakeClock->advance(ShootoutManager::kConfirmRebroadcastMs + 1);
    suite->tick();
}

inline void memberTakesItsHeadsRosterDuringTheProposal(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> head = {0x06, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(head);
    suite->shootout->onRingClosedReceived(head.data(), {me});
    suite->shootout->startProposal();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);

    suite->shootout->onRingClosedReceived(other.data(), {me, other});
    EXPECT_EQ(suite->shootout->getRingMembers().size(), 1u)
        << "a member took its roster from a device that is not its head";

    suite->shootout->onRingClosedReceived(head.data(), {me, head, other});
    EXPECT_EQ(suite->shootout->getRingMembers().size(), 3u)
        << "a member kept a roster its head's rebroadcast replaces";
}

inline void followerReacksItsCoordinatorsBracketRetransmit(ShootoutManagerTests* suite) {
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x06, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->joinRelayedRing(coord.data());
    suite->shootout->setRingMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    suite->shootout->onBracketReceived(coord.data(), {me, coord, other}, 1);
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), coord.data(), 0, 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kReliableAck, ackFor(PktType::kShootoutBracket),
                         sizeof(ReliableAckPayload)))
    .Times(1)
    .WillOnce(testing::Return(1));

    suite->shootout->onBracketReceived(coord.data(), {me, coord, other}, 1);

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), coord.data(), 6), 0);
}

inline void abortedRingReclaimsWhileStillCabled(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    int ringClosedSends = 0;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutRingClosed, testing::_, testing::_))
    .WillByDefault(
    testing::Invoke([&ringClosedSends](const uint8_t*, PktType, const uint8_t*, const size_t) {
        ringClosedSends++;
        return 1;
    }));

    suite->headRingServing({{0x02, 0, 0, 0, 0, 0}});

    suite->shootout->onRingClosed();
    ASSERT_TRUE(suite->shootout->shouldEnterProposal());

    // Abort back to idle without touching a cable. The RDC latch stays set and is
    // edge-triggered, so it will never announce this ring again.
    suite->shootout->resetToIdle();
    ASSERT_EQ(suite->memberRdc.getChainRole(), ChainRole::RING);

    // Idle polls this predicate every tick and mounts the proposal on it. Both
    // halves have to work, so drive those rather than the manager's sync().
    ASSERT_TRUE(suite->shootout->shouldEnterProposal());
    ringClosedSends = 0;
    suite->shootout->startProposal();

    EXPECT_EQ(ringClosedSends, 1) << "the still-closed ring was not re-announced";
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
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
            sendData(testing::_, PktType::kShootoutRingClosed, testing::_, testing::_))
    .WillByDefault(testing::Invoke(
    [&lastRingClosed](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        lastRingClosed.assign(data, data + len);
        return 1;
    }));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_TRUE(suite->shootout->getBracket().empty());

    // The absent member's announce lands, and the next re-announce round names it
    // in the frame.
    suite->shootout->setRingMembersForTest({me, peer});
    suite->fakeClock->advance(ShootoutManager::kConfirmRebroadcastMs + 100);
    suite->tick();
    ASSERT_EQ(lastRingClosed.size(), 2u + 2 * 6);
    ShootoutRosterPayload roster{};
    memcpy(&roster, lastRingClosed.data(), lastRingClosed.size());
    ASSERT_EQ(roster.count, 2u);
    EXPECT_EQ(memcmp(roster.macs[1], peer.data(), 6), 0);

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
            sendData(testing::_, PktType::kShootoutRingClosed, testing::_, testing::_))
    .WillByDefault(
    testing::Invoke([&ringClosedFrames](const uint8_t*, PktType, const uint8_t*, const size_t) {
        ringClosedFrames++;
        return 1;
    }));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me, peer});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    ASSERT_EQ(ringClosedFrames, 1);

    for (int i = 0; i < 30; i++) {
        suite->fakeClock->advance(100);
        suite->tick();
    }
    EXPECT_GT(ringClosedFrames, 1);

    // Once everyone has confirmed there is nobody left to reach.
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(peer.data());
    int afterConfirmed = ringClosedFrames;
    for (int i = 0; i < 30; i++) {
        suite->fakeClock->advance(100);
        suite->tick();
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
        suite->shootout->setRingMembersForTest(members);
        suite->shootout->resetToIdle();
        suite->shootout->onRingClosed();
        suite->shootout->startProposal();
        suite->shootout->confirmLocal();
        for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
        auto bracket = suite->shootout->getBracket();
        EXPECT_EQ(bracket.size(), memberCount);
        EXPECT_EQ(suite->shootout->hasBye(), expectBye);
    };
    suite->closeRingOnJacks();
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
    suite->shootout->setRingMembersForTest({m1, m2, m3});
    suite->closeRingOnJacks();
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
    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest(members);

    // The fan-out is a single broadcast frame, not one unicast per member, and
    // it still owes an ack from each of the two peers.
    std::vector<std::array<uint8_t, 6>> destinations;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutBracket, testing::_, testing::_))
    .WillByDefault(testing::Invoke([suite, &destinations](const uint8_t* dst, PktType type,
                                                          const uint8_t* data, const size_t len) {
        suite->recordFrame(type, data, len);
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
    EXPECT_EQ(
    suite->pending(PktType::kShootoutBracket, suite->lastSeqId(PktType::kShootoutBracket)), 2u);
    ASSERT_EQ(destinations.size(), 1u);
    EXPECT_EQ(memcmp(destinations[0].data(), MockDevice::BROADCAST_MAC, 6), 0);
}

inline void bracketAckClearsPendingForThatPeer(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}
    };
    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest(members);
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    uint8_t seqId = suite->lastSeqId(PktType::kShootoutBracket);
    EXPECT_EQ(suite->pending(PktType::kShootoutBracket, seqId), 2u);
    suite->ack(PktType::kShootoutBracket, members[1].data(), seqId);
    EXPECT_EQ(suite->pending(PktType::kShootoutBracket, seqId), 1u);
    suite->ack(PktType::kShootoutBracket, members[2].data(), seqId);
    EXPECT_EQ(suite->pending(PktType::kShootoutBracket, seqId), 0u);
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
    suite->shootout->setRingMembersForTest(members);
    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    for (int i = 0; i < 60; i++) {
        suite->fakeClock->advance(100);
        suite->tick();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

inline void matchStartWaitsOutTheBracketRevealWindow(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01,0,0,0,0,0}, {0x02,0,0,0,0,0}, {0x03,0,0,0,0,0}, {0x04,0,0,0,0,0}
    };
    // The RDC has to agree the ring is closed: this case runs the loop for long
    // enough that a ring only the manager believes in reads as one that broke.
    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest(members);

    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();

    auto bracket = suite->shootout->getBracket();
    uint8_t bracketSeq = suite->lastSeqId(PktType::kShootoutBracket);

    // Ack from only one peer, kept inside the fan-out's span: past it the silent
    // members are given up on, which ends the tournament rather than delaying the
    // match. What holds the start here is the reveal window, not the ack gate —
    // kBracketRevealMs outlasts the span, so no timeline reaches an expired reveal
    // with the bracket still owed acks.
    for (const auto& m : bracket) {
        if (memcmp(m.data(), selfMac, 6) != 0) {
            suite->ack(PktType::kShootoutBracket, m.data(), bracketSeq);
            break;
        }
    }
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS - 100);
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // The rest ack in time, so nobody is dropped and the match starts once the
    // bracket has been on screen for its reveal window. Asserted the tick it
    // starts: from there MATCH_START is the fan-out owed acks, and nobody in this
    // case answers it either.
    for (const auto& m : bracket) {
        if (memcmp(m.data(), selfMac, 6) == 0) continue;
        suite->ack(PktType::kShootoutBracket, m.data(), bracketSeq);
    }
    for (int step = 0;
         step < 400 && suite->shootout->getPhase() != ShootoutManager::Phase::MATCH_IN_PROGRESS;
         ++step) {
        suite->fakeClock->advance(20);
        suite->tick();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), 0);
    auto pair = suite->shootout->getCurrentMatchPair();
    EXPECT_EQ(pair.first, bracket[0]);
    EXPECT_EQ(pair.second, bracket[1]);
}

inline void nonCoordinatorReceivingMatchStartIdentifiesRole(ShootoutManagerTests* suite) {
    // Receiver of MATCH_START decides participant-vs-spectator from the carried
    // match pair. Cover both branches by running two MATCH_STARTs in the
    // same fixture: first names self (in match), second names two peers
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

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({me, coord, other});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(other.data());
    suite->shootout->onBracketReceived(coord.data(), {me, coord, other}, 1);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);

    // Self in the match pair -> isInCurrentMatch + opponentMac populated.
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), coord.data(), 0, 2);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    EXPECT_TRUE(suite->shootout->isInCurrentMatch());
    EXPECT_EQ(memcmp(suite->shootout->getOpponentMac().data(), coord.data(), 6), 0);

    // Different MATCH_START where self is NOT in the pair -> spectator.
    suite->shootout->onMatchStartReceived(coord.data(), coord.data(), other.data(), 1, 3);
    EXPECT_FALSE(suite->shootout->isInCurrentMatch());
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
    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, opMac});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->onBracketReceived(coord.data(), {me, opMac, coord}, 1);
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), opMac.data(), 0, 2);

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
    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, aMac, bMac, me});
    suite->shootout->startProposal();
    for (auto& m : std::vector<std::array<uint8_t,6>>{coord, aMac, bMac, me}) {
        suite->shootout->onConfirmReceived(m.data());
    }
    suite->shootout->onBracketReceived(coord.data(), {aMac, bMac, coord, me}, 1);
    suite->shootout->onMatchStartReceived(coord.data(), aMac.data(), bMac.data(), 0, 2);
    suite->shootout->onMatchResultReceived(aMac.data(), bMac.data(), 0, 3, aMac.data());
    EXPECT_TRUE(suite->shootout->isEliminated(bMac.data()));
    EXPECT_FALSE(suite->shootout->isEliminated(aMac.data()));
}

// Every match gets its own attempt. A device-wide flag would be spent by the
// first match this device wins and never returned, so a later lost result would
// go unrecovered on the same device — and on every later tournament in the same
// power cycle.
inline void eachMatchGetsItsOwnResultRetry(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> fourth = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other, fourth});
    suite->shootout->startProposal();
    for (const auto& m : {coord, me, other, fourth})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(coord.data(), {coord, me, other, fourth}, 1);

    suite->shootout->onMatchStartReceived(coord.data(), me.data(), other.data(), 0, 2);
    suite->shootout->reportLocalWin();
    const uint8_t firstSeq = suite->lastSeqId(PktType::kShootoutMatchResult);
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
    ASSERT_NE(suite->lastSeqId(PktType::kShootoutMatchResult), firstSeq)
    << "first match was not recovered at all";

    // A later match on the same device. Its attempt must not have been spent.
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), fourth.data(), 1, 3);
    suite->shootout->reportLocalWin();
    const uint8_t secondSeq = suite->lastSeqId(PktType::kShootoutMatchResult);
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);

    EXPECT_NE(suite->lastSeqId(PktType::kShootoutMatchResult), secondSeq)
    << "the second match this device won got no retry; the latch was a "
       "device-wide latch, not one per match";
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a single lost ack on a later match ended the whole tournament";
}

// A result for an older match must not pull this device out of the one it is
// fighting now. Senders re-send when the coordinator misses a result, so a late
// copy can land on a device that has since been paired again.
inline void staleResultDoesNotEndTheCurrentMatch(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> fourth = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other, fourth});
    suite->shootout->startProposal();
    for (const auto& m : {coord, me, other, fourth})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(coord.data(), {coord, me, other, fourth}, 1);

    // Missed match 0's result, now mid-match in match 1.
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), fourth.data(), 1, 5);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // Match 0's winner re-sends after the coordinator missed it.
    suite->shootout->onMatchResultReceived(coord.data(), other.data(), 0, 9, coord.data());

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS)
        << "a late result for an older match ended the match this device is fighting";
    EXPECT_TRUE(suite->shootout->isEliminated(other.data()))
        << "the elimination itself should still be recorded";
}

inline void coordinatorMissingOurResultIsRecoveredBySender(ShootoutManagerTests* suite) {
    // A result the coordinator never takes is the one failure the retry machinery
    // cannot see from its end: it advances the bracket only on receiving one, so
    // it sits in MATCH_IN_PROGRESS with nothing owed and nothing to give up on.
    // The winner is the device that knows, because its own fan-out is what
    // abandoned — so it says so again instead of waiting to be asked.
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(other.data());
    suite->shootout->onBracketReceived(coord.data(), {coord, me, other}, 1);

    suite->shootout->onMatchStartReceived(coord.data(), me.data(), other.data(), 0, 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    suite->shootout->reportLocalWin();
    // Count the frame, not the retransmits: a retry re-sends the SAME seqId, so
    // only a fresh one distinguishes a genuine re-send from the retry machinery
    // doing its ordinary job.
    const uint8_t firstSeq = suite->lastSeqId(PktType::kShootoutMatchResult);
    ASSERT_NE(firstSeq, 0u);

    // Nobody acks. The fan-out runs out its span and gives up on the coordinator.
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);

    EXPECT_NE(suite->lastSeqId(PktType::kShootoutMatchResult), firstSeq)
    << "the coordinator never took our result and nothing re-sent it";
}

inline void matchStartRetriesToSilentMemberThenAborts(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    int matchStartFrames = 0;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutMatchStart, testing::_, testing::_))
    .WillByDefault(
    testing::Invoke([&matchStartFrames](const uint8_t*, PktType, const uint8_t*, const size_t) {
        matchStartFrames++;
        return 1;
    }));

    suite->driveToFirstMatch({me, opMac});
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    ASSERT_EQ(matchStartFrames, 1);

    // The peer never acks, so the frame keeps going out on its doubling backoff:
    // 100, 300 and 700ms in — the gaps double, so the offsets are their running
    // sum — with the next one due past the span.
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS - 100);
    EXPECT_EQ(matchStartFrames, 4);  // 1 initial + 3 retransmits at 100/300/700
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // Span spent, and the silent member is one of the two fighting: it ends.
    suite->runLoopFor(200);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

// A tournament that ends says one thing and nothing else. Retransmits outlive
// the state that produced them unless the reset cancels them, and a MATCH_START
// landing after an abort would speak for a bracket that no longer exists.
inline void resetCancelsInFlightFanOuts(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    int abortFrames = 0;
    int tournamentFrames = 0;
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault(
    testing::Invoke([suite, &abortFrames, &tournamentFrames](
                    const uint8_t*, PktType type, const uint8_t* data, const size_t len) {
        suite->recordFrame(type, data, len);
        switch (type) {
            case PktType::kShootoutAbort: abortFrames++; break;
            case PktType::kShootoutRingClosed:
            case PktType::kShootoutConfirm:
            case PktType::kShootoutBracket:
            case PktType::kShootoutMatchStart:
            case PktType::kShootoutMatchResult:
            case PktType::kShootoutTournamentEnd: tournamentFrames++; break;
            default: break;
        }
        return 1;
    }));

    suite->driveToFirstMatch({me, opMac});
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    ASSERT_GT(
    suite->pending(PktType::kShootoutMatchStart, suite->lastSeqId(PktType::kShootoutMatchStart)),
    0u);

    suite->shootout->abortTournament();
    EXPECT_EQ(
    suite->pending(PktType::kShootoutMatchStart, suite->lastSeqId(PktType::kShootoutMatchStart)),
    0u);

    // The dead tournament's fan-outs are cancelled and stay cancelled; the ABORT
    // that killed it is armed instead, and retrying is how a member that missed
    // the first frame is told to stop.
    const int tournamentAtAbort = tournamentFrames;
    const int abortAtAbort = abortFrames;
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
    EXPECT_EQ(tournamentFrames, tournamentAtAbort)
        << "a cancelled tournament fan-out kept transmitting";
    EXPECT_GT(abortFrames, abortAtAbort)
        << "the ABORT was not retried, so a member that missed it stays in the tournament";
}

// A teardown zeroes the current pair, and the spectator repaints whenever the
// pair changes. Those two together put WATCHING over two blank names on screen
// for the tick between the abort and the transition off this state.
inline void spectatorDoesNotRepaintATornDownMatch(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
    // drawCenteredText chains off these, so a default-constructed nullptr return
    // dereferences on the first draw.
    MockDisplay* display = suite->device.mockDisplay;
    ON_CALL(*display, invalidateScreen()).WillByDefault(testing::Return(display));
    ON_CALL(*display, setGlyphMode(testing::_)).WillByDefault(testing::Return(display));
    ON_CALL(*display, drawText(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(display));
    ON_CALL(*display, getWidth()).WillByDefault(testing::Return(128));
    ON_CALL(*display, getTextWidth(testing::_)).WillByDefault(testing::Return(40));

    suite->driveToFirstMatch({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}});
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    GameContext ctx;
    ctx.shootoutManager = suite->shootout;
    ShootoutSpectator state(ctx);
    state.onStateMounted(&suite->device);

    suite->shootout->abortTournament();
    EXPECT_CALL(*display, render()).Times(0);
    state.onStateLoop(&suite->device);
}

inline void ackNamingNoLiveFrameClearsNobody(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->shootout->setRingMembersForTest({me, opMac});
    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    const uint8_t bracketSeq = suite->lastSeqId(PktType::kShootoutBracket);
    ASSERT_EQ(suite->pending(PktType::kShootoutBracket, bracketSeq), 1u);

    suite->ack(PktType::kShootoutBracket, opMac.data(), static_cast<uint8_t>(bracketSeq + 7));
    EXPECT_EQ(suite->pending(PktType::kShootoutBracket, bracketSeq), 1u);

    suite->ack(PktType::kShootoutBracket, opMac.data(), bracketSeq);
    EXPECT_EQ(suite->pending(PktType::kShootoutBracket, bracketSeq), 0u);
}

// A late MATCH_START for a finished match — the coordinator advances without
// waiting on that fan-out, so an old frame can still be retrying to a member
// whose seqId cursor has already moved on. Being
// dragged back would re-prime it against an opponent it already beat, and a
// second result for that match can eliminate the winner too.
inline void reAnnouncedMatchIsNotReplayedOnceFinished(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    // Every member's CONFIRM is broadcast and every device in PROPOSAL records
    // it, so a follower knows the whole ring — which is who a result goes to.
    // Without this the set holds only this device, and a result reaches nobody.
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(other.data());
    suite->shootout->onBracketReceived(coord.data(), {coord, me, other}, 1);

    // This device fights match 0 and wins it. Reported the way a real winner
    // reports — the quickdraw outcome, not a result arriving from elsewhere —
    // because that is what leaves this device holding the record.
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), other.data(), 0, 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    suite->shootout->reportLocalWin();
    ASSERT_TRUE(suite->shootout->isEliminated(other.data()));
    ASSERT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // The coordinator never saw that result and re-announces match 0.
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), other.data(), 0, 9);

    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS)
        << "dragged back into a match it had already won";
    EXPECT_TRUE(suite->shootout->isEliminated(other.data()))
        << "the beaten opponent was put back in the running";
}

// A tournament that reached its winner is finished, not stuck. A fan-out from
// the final match can outlive it and give up afterwards, and tearing the
// standings down then would also broadcast ABORT — which every member applies
// from ENDED, wiping the winner screen across the whole ring.
inline void abortDoesNotTearDownAFinishedTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me});
    suite->shootout->startProposal();
    suite->shootout->onBracketReceived(coord.data(), {coord, me}, 1);
    suite->shootout->onTournamentEndReceived(coord.data(), me.data(), 40);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);

    suite->shootout->abortTournament();

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED)
        << "a finished tournament was aborted";
    EXPECT_EQ(memcmp(suite->shootout->getTournamentWinner().data(), me.data(), 6), 0)
        << "the winner was lost";
}

// A fan-out outlives the match it announced: it keeps retrying a silent recipient
// for over a second, and a match can finish inside that window. So the question
// "does this silence block the tournament" has to be asked of the frame that was
// abandoned, not of whatever match is running by the time it gives up — or a
// device that merely sat out match 0 gets judged as a participant of match 1.
inline void abandonedMatchStartIsJudgedAgainstItsOwnMatch(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> d = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->driveToFirstMatch({me, b, c, d});
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> first =
        suite->shootout->getCurrentMatchPair();
    ASSERT_NE(memcmp(first.first.data(), me.data(), 6), 0)
        << "coordinator unexpectedly in match 0 — shuffle seed drifted";

    // Only the two fighting match 0 answer. The others are spectators for it.
    const uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, first.first.data(), msSeq);
    suite->ack(PktType::kShootoutMatchStart, first.second.data(), msSeq);
    ASSERT_GT(suite->pending(PktType::kShootoutMatchStart, msSeq), 0u);

    // Match 0 finishes and the bracket moves on while that fan-out is still live.
    suite->shootout->onMatchResultReceived(first.first.data(), first.second.data(), 0,
                                           static_cast<uint8_t>(msSeq + 1),
                                           first.first.data());

    // Match 1 starts and everybody answers for it, so the only frame still owed
    // anything is match 0's — the stale one.
    suite->tick();
    const uint8_t nextSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    ASSERT_NE(nextSeq, msSeq) << "match 1 never started";
    for (auto& m : {b, c, d})
        suite->ack(PktType::kShootoutMatchStart, m.data(), nextSeq);
    ASSERT_EQ(suite->pending(PktType::kShootoutMatchStart, nextSeq), 0u);
    ASSERT_GT(suite->pending(PktType::kShootoutMatchStart, msSeq), 0u);

    // Now the stale fan-out gives up on a spectator of match 0 who is very
    // likely fighting match 1.
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a stale fan-out was judged against the match running now";
}

// A spectator is not fighting this match, so its silence on MATCH_START must not
// end the tournament. Without this the abort surface is every member of every
// match rather than the two devices the match actually depends on.
inline void silentSpectatorDoesNotAbortMatchStart(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> d = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->driveToFirstMatch({me, b, c, d});
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // Both fighters ack; the members not in this match stay silent.
    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> pair =
        suite->shootout->getCurrentMatchPair();
    uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, pair.first.data(), msSeq);
    suite->ack(PktType::kShootoutMatchStart, pair.second.data(), msSeq);

    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a spectator's silence ended a match it was not fighting";
}

inline void finalMatchResultTriggersTournamentEnd(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
    suite->shootout->setRingMembersForTest({me, opMac});
    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    // Self is coord; bracket is already set. Ack bracket from peer.
    uint8_t bSeq = suite->lastSeqId(PktType::kShootoutBracket);
    suite->ack(PktType::kShootoutBracket, opMac.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->tick();  // fires MATCH_START 0
    uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, opMac.data(), msSeq);
    // Self wins.
    // reportLocalWin → applyMatchResult → BETWEEN_MATCHES. The coordinator then
    // finds no more pairs on its next tick and broadcasts TOURNAMENT_END.
    suite->shootout->reportLocalWin();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BETWEEN_MATCHES)
        << "the round advanced from inside the call that reported the win";
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    EXPECT_EQ(memcmp(suite->shootout->getTournamentWinner().data(), me.data(), 6), 0);
}

inline void startProposalClearsAllPriorTournamentState(ShootoutManagerTests* suite) {
    // Run tournament 1 to ENDED, then call startProposal again and verify
    // that no state from tournament 1 leaks into tournament 2.
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    // Tournament 1
    suite->shootout->setRingMembersForTest({me, opMac});
    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    uint8_t bSeq = suite->lastSeqId(PktType::kShootoutBracket);
    suite->ack(PktType::kShootoutBracket, opMac.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->tick();
    uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, opMac.data(), msSeq);
    suite->shootout->reportLocalWin();
    suite->shootout->sync();
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
    EXPECT_EQ(
    suite->pending(PktType::kShootoutBracket, suite->lastSeqId(PktType::kShootoutBracket)), 0u);
}

inline void confirmRecordsPeerName(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    suite->shootout->setRingMembersForTest({
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
    // ESP-NOW link-layer duplicate delivery presents the same MATCH_RESULT
    // twice. Both must land on one elimination: counting the loser twice
    // advances the round early and collapses a 4-member bracket to 3.
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me  = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b   = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c   = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> d   = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
    suite->shootout->setRingMembersForTest({me, b, c, d});
    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : {me, b, c, d}) suite->shootout->onConfirmReceived(m.data());
    uint8_t bSeq = suite->lastSeqId(PktType::kShootoutBracket);
    for (auto& m : {b, c, d})
        suite->ack(PktType::kShootoutBracket, m.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->tick();
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
    suite->shootout->sync();
    suite->shootout->onMatchResultReceived(winner, loser, 0, /*seqId=*/7, winner);
    suite->shootout->sync();

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

    std::atomic<int> sendCount{0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault(testing::Invoke(
    [suite, &sendCount](const uint8_t*, PktType type, const uint8_t* data, const size_t len) {
        suite->recordFrame(type, data, len);
        sendCount.fetch_add(1);
        return 1;
    }));

    suite->shootout->setRingMembersForTest({me, opMac});
    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    uint8_t bSeq = suite->lastSeqId(PktType::kShootoutBracket);
    suite->ack(PktType::kShootoutBracket, opMac.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->tick();
    uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, opMac.data(), msSeq);
    suite->shootout->reportLocalWin();
    suite->shootout->sync();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    ASSERT_EQ(suite->pending(PktType::kShootoutTournamentEnd,
                             suite->lastSeqId(PktType::kShootoutTournamentEnd)),
              1u);

    // Snapshot send count immediately after the initial TOURNAMENT_END
    // broadcast — anything further must come from the retry path.
    int sendCountAfterInitialBroadcast = sendCount.load();

    suite->fakeClock->advance(200);
    suite->tick();
    EXPECT_GT(sendCount.load(), sendCountAfterInitialBroadcast);
    EXPECT_EQ(suite->pending(PktType::kShootoutTournamentEnd,
                             suite->lastSeqId(PktType::kShootoutTournamentEnd)),
              1u);

    // Correct ack clears the pending entry.
    uint8_t teSeq = suite->lastSeqId(PktType::kShootoutTournamentEnd);
    suite->ack(PktType::kShootoutTournamentEnd, opMac.data(), teSeq);
    EXPECT_EQ(suite->pending(PktType::kShootoutTournamentEnd, teSeq), 0u);
}

inline void matchResultRetriesUntilAcked(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> spec  = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    std::atomic<int> sendCount{0};
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault(testing::Invoke(
    [suite, &sendCount](const uint8_t*, PktType type, const uint8_t* data, const size_t len) {
        suite->recordFrame(type, data, len);
        sendCount.fetch_add(1);
        return 1;
    }));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me, opMac, spec});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->onConfirmReceived(spec.data());
    uint8_t bSeq = suite->lastSeqId(PktType::kShootoutBracket);
    suite->ack(PktType::kShootoutBracket, opMac.data(), bSeq);
    suite->ack(PktType::kShootoutBracket, spec.data(), bSeq);
    suite->fakeClock->advance(6000);
    suite->tick();
    uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, opMac.data(), msSeq);
    suite->ack(PktType::kShootoutMatchStart, spec.data(), msSeq);

    // Self wins match 0 → broadcasts MATCH_RESULT to opMac and spec.
    suite->shootout->reportLocalWin();
    EXPECT_EQ(
    suite->pending(PktType::kShootoutMatchResult, suite->lastSeqId(PktType::kShootoutMatchResult)),
    2u);

    int sendCountAfterInitial = sendCount.load();

    suite->fakeClock->advance(200);
    suite->tick();
    EXPECT_GT(sendCount.load(), sendCountAfterInitial);
    EXPECT_EQ(
    suite->pending(PktType::kShootoutMatchResult, suite->lastSeqId(PktType::kShootoutMatchResult)),
    2u);

    // Acks clear pending.
    const uint8_t resultSeq = suite->lastSeqId(PktType::kShootoutMatchResult);
    suite->ack(PktType::kShootoutMatchResult, opMac.data(), resultSeq);
    EXPECT_EQ(suite->pending(PktType::kShootoutMatchResult, resultSeq), 1u);
    suite->ack(PktType::kShootoutMatchResult, spec.data(), resultSeq);
    EXPECT_EQ(suite->pending(PktType::kShootoutMatchResult, resultSeq), 0u);
}

inline void shootoutLeavesStandingRoleAlone(ShootoutManagerTests* suite) {
    // The draw slot a bracket match assigns belongs to that match. Writing it to
    // the Player instead made the restore at the next match boundary land on a
    // match that was still running, and the loser then read the winner's slot.
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};  // < self -> bounty slot
    std::array<uint8_t, 6> third = {0x07, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    MockStorage storage;
    MatchManager matchManager(&suite->player, &storage, nullptr, nullptr);
    suite->followRingHead(opMac, &matchManager);

    suite->player.setIsHunter(true);

    suite->shootout->setRingMembersForTest({me, opMac, third});
    suite->shootout->startProposal();
    for (const std::array<uint8_t, 6>& m : {me, opMac, third})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(opMac.data(), {me, opMac, third}, 1);
    suite->shootout->onMatchStartReceived(opMac.data(), me.data(), opMac.data(), 0, 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    EXPECT_FALSE(matchManager.isLocalHunter())
        << "the match's draw slot did not come from MAC ordering";
    EXPECT_TRUE(suite->player.isHunter())
        << "the tournament overwrote the standing role with a per-match slot";

    // And it stays put across the match boundary, where a restore would land.
    suite->shootout->onMatchResultReceived(opMac.data(), me.data(), 0, 3, opMac.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BETWEEN_MATCHES);
    EXPECT_TRUE(suite->player.isHunter());
    EXPECT_FALSE(matchManager.isLocalHunter())
    << "the match's draw slot was rewritten underneath a live match";

    matchManager.clearCurrentMatch();
    suite->rebuildShootout(nullptr);
}

// One execDrivers drain can carry both a MATCH_START and the ABORT that ends the
// tournament, and the match app is only entered from a state loop — so the app
// dismount that retires a match never runs. Idle mounts a match from whatever
// match is ready, and a match left behind is scored against stale draw times;
// the tournament after it cannot prime over one either.
inline void anAbortRetiresTheMatchTheTournamentPrimed(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> third = {0x07, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    MockStorage storage;
    MatchManager matchManager(&suite->player, &storage, nullptr, nullptr);
    suite->rebuildShootout(&matchManager);

    suite->joinRelayedRing(coord.data());
    suite->shootout->setRingMembersForTest({me, coord, third});
    suite->shootout->startProposal();
    for (const std::array<uint8_t, 6>& m : {me, coord, third})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(coord.data(), {me, coord, third}, 1);
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), coord.data(), 0, 2);
    ASSERT_TRUE(matchManager.isMatchReady()) << "MATCH_START did not prime the match";

    suite->shootout->onAbortReceived(third.data(), 9);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);

    EXPECT_FALSE(matchManager.isMatchReady())
    << "Idle mounts a phantom match from the abandoned match";
    EXPECT_FALSE(matchManager.getCurrentMatch().has_value())
        << "the next tournament cannot prime a match over the abandoned one";

    suite->rebuildShootout(nullptr);
}

// The match the cable handshake owns is not the tournament's to retire, and a
// tournament can end while one is parked beside it.
inline void aTournamentResetLeavesACableMatchAlone(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    uint8_t opponent[6] = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    MockStorage storage;
    MatchManager matchManager(&suite->player, &storage, nullptr, nullptr);
    suite->rebuildShootout(&matchManager);

    matchManager.receiveMatch("b5f1c0de-0000-4000-8000-00000000cafe", /*opponentIsHunter=*/true,
                              opponent);
    ASSERT_TRUE(matchManager.getCurrentMatch().has_value());

    suite->shootout->resetToIdle();

    EXPECT_TRUE(matchManager.getCurrentMatch().has_value())
        << "a tournament ending took the cable handshake's match with it";

    matchManager.clearCurrentMatch();
    suite->rebuildShootout(nullptr);
}

// The two terminal screens run resetToIdle on dismount, which is what lets the
// next ring closure propose a fresh tournament. That reset cancels the shootout
// fan-outs in flight — but not the one reporting the ending, whose recipients are
// exactly the members that have not yet heard it. TOURNAMENT_END is the sharper
// case of the two: the standings screen leaves on the first cable pull, with no
// dwell timer, so without the exemption a pull inside the retransmit span drops
// the news to anyone who missed the first frame.
inline void endingSurvivesTheTerminalScreenExit(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->driveToFirstMatch({me, opMac});
    const uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, opMac.data(), msSeq);
    suite->shootout->reportLocalWin();
    suite->shootout->sync();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    const uint8_t endSeq = suite->lastSeqId(PktType::kShootoutTournamentEnd);
    ASSERT_GT(suite->pending(PktType::kShootoutTournamentEnd, endSeq), 0u);

    // What ShootoutFinalStandings::onStateDismounted does when the ring opens.
    suite->shootout->resetToIdle();

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::IDLE);
    EXPECT_GT(suite->pending(PktType::kShootoutTournamentEnd, endSeq), 0u)
    << "leaving the standings screen cancelled the TOURNAMENT_END still owed "
       "to a member that never acked it";
}

// The ending has to outlive every reset between it and its last retransmit, not
// just the first. On an abandonment abort the ring stays cabled, so the device
// walks the ABORTED screen straight back into a fresh proposal — and that
// proposal's reset used to cancel the ABORT still in flight, leaving the whole
// thing resting on the screen's dwell outlasting the retry schedule.
inline void theEndingOutlivesAReProposal(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->driveToFirstMatch({me, opMac});
    const uint8_t msSeq = suite->lastSeqId(PktType::kShootoutMatchStart);
    suite->ack(PktType::kShootoutMatchStart, opMac.data(), msSeq);
    suite->shootout->reportLocalWin();
    suite->shootout->sync();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    const uint8_t endSeq = suite->lastSeqId(PktType::kShootoutTournamentEnd);
    ASSERT_GT(suite->pending(PktType::kShootoutTournamentEnd, endSeq), 0u);

    suite->shootout->resetToIdle();    // the standings screen dismounting
    suite->shootout->startProposal();  // the ring is still closed, so straight back in

    EXPECT_GT(suite->pending(PktType::kShootoutTournamentEnd, endSeq), 0u)
    << "re-proposing cancelled the ending still owed to a member that never acked";
}

// Same rule for the abort half, where the screen does have a dwell timer — so
// this pins the exemption rather than the timer outlasting the retries.
inline void anAbortSurvivesTheAbortedScreenExit(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault(
    testing::Invoke([suite](const uint8_t*, PktType type, const uint8_t* data, const size_t len) {
        suite->recordFrame(type, data, len);
        return 1;
    }));

    suite->driveToFirstMatch({me, opMac});
    suite->shootout->abortTournament();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    const uint8_t abortSeq = suite->lastSeqId(PktType::kShootoutAbort);
    ASSERT_NE(abortSeq, 0);
    const size_t owed = suite->pending(PktType::kShootoutAbort, abortSeq);
    ASSERT_GT(owed, 0u);

    // What ShootoutAborted::onStateDismounted does when its display timer expires.
    suite->shootout->resetToIdle();

    EXPECT_EQ(suite->pending(PktType::kShootoutAbort, abortSeq), owed)
    << "leaving the ABORTED screen cancelled the ABORT it was displaying";
}

// A match resolving on the same tick as an abort must not walk the phase back.
// The abort edge is read after this in the tick, so a walk-back leaves the device
// in a tournament every other member has torn down — and on the abandonment path
// the ring is still closed, so no ring-break guard will fire to correct it.
inline void aLocalWinDoesNotWalkBackAnAbortedTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->driveToFirstMatch({me, opMac});
    suite->shootout->abortTournament();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);

    suite->shootout->reportLocalWin();

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a match resolving after the abort reopened the tournament";
}

// A ring that cannot field two players has nothing to draw a bracket from, and
// allMembersConfirmed refuses a short roster — so without a deadline the
// coordinator waits on a confirm tally that can never complete. The ring is
// closed on the jacks throughout: a device cabled to itself is a closed ring of
// one, and the ring-break guard must not be what ends this.
inline void aRingTooSmallToPlayGivesUpOnTheProposal(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "a lone confirm should not start anything on its own";

    // Arms the window. heldFor is level-triggered and production samples it every
    // tick, so a test that advances the clock before the first sample would be
    // measuring from the wrong instant.
    suite->shootout->sync();

    // Short of the deadline it is still waiting — the roster may yet fill.
    suite->fakeClock->advance(ShootoutManager::SHORT_ROSTER_TIMEOUT_MS - 500);
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "gave up on the proposal before the roster had stopped filling";

    suite->fakeClock->advance(1000);
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a ring of one waited on a confirm tally that can never complete";
}

// The deadline must not punish a roster that was merely slow. Announces are still
// in flight for the first moments after a ring closes, which is the whole reason
// the abort is held over a window rather than fired the instant the tally reads
// short.
inline void aRosterThatFillsInsideTheWindowStillRuns(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->sync();

    suite->fakeClock->advance(ShootoutManager::SHORT_ROSTER_TIMEOUT_MS - 500);
    suite->shootout->sync();

    // The second member's announce lands late, inside the window.
    suite->shootout->setRingMembersForTest({me, other});
    suite->fakeClock->advance(2000);
    suite->shootout->sync();

    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a roster that filled before the deadline was still given up on";
}

inline void aBracketDoesNotReopenAnEndedTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({me, coord});
    suite->shootout->onRingClosedReceived(coord.data(), {me, coord});
    suite->shootout->onBracketReceived(coord.data(), {me, coord}, 1);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);

    suite->shootout->onAbortReceived(coord.data(), 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);

    // The coordinator's BRACKET retry, arriving after we gave up.
    suite->shootout->onBracketReceived(coord.data(), {me, coord}, 1);

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a retransmitted bracket pulled an aborted device back into the tournament";
}

// The short-roster window is only sampled while the phase is PROPOSAL, so a
// tournament that leaves that phase mid-window must not carry a part-aged timer
// into the next one — DebouncedCondition is level-triggered and its timer keeps
// running against the wall clock whether or not anyone is looking.
inline void aStaleShortRosterWindowDoesNotAbortTheNextProposal(
    ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me});
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->sync();  // arms the window against a roster of one

    // The proposal ends some other way before the window elapses, and enough time
    // passes for the abandoned window to have run out unwatched.
    suite->shootout->resetToIdle();
    suite->fakeClock->advance(ShootoutManager::SHORT_ROSTER_TIMEOUT_MS * 2);

    // A fresh proposal, still short-rostered — so the condition holds again and a
    // stale timer would report it as having held all along.
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->sync();

    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a window armed by the previous proposal fired on the first tick of this one";

    // The fresh window still works; it just has to be waited out.
    suite->fakeClock->advance(ShootoutManager::SHORT_ROSTER_TIMEOUT_MS + 1);
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "resetting the window disarmed it for good";
}

// A player still mounted in a match when TOURNAMENT_END lands resolves its own match and
// reports it. That result must not walk a crowned tournament back into a round:
// the winner is already eliminated by it, the next survivor scan finds nobody,
// and the no-survivors abort tears down standings the ring has already seen.
inline void aLateResultDoesNotReopenACrownedTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({me, other});
    suite->shootout->startProposal();
    for (const std::array<uint8_t, 6>& m : {me, other})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onTournamentEndReceived(me.data(), me.data(), 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);

    suite->shootout->onMatchResultReceived(other.data(), me.data(), 0, 3, other.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED)
        << "a late result reopened a tournament that had already crowned a winner";
}

// A bracket with nobody left cannot name a winner. A TOURNAMENT_END naming the
// all-zero MAC is acked and dropped by every receiver, so nobody reaches ENDED
// and the ring sits in BETWEEN_MATCHES for good. Reaching zero survivors takes a
// stale result: one tagged with an index that is no longer current is recorded
// without ending the match, so it can take a finalist out while the survivor scan
// is still gated.
inline void tournamentWithNoSurvivorsAbortsInsteadOfNamingNobody(
    ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // coord
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> d = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    suite->driveToFirstMatch({me, b, c, d});
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> round1a =
        suite->shootout->getCurrentMatchPair();
    suite->shootout->onMatchResultReceived(round1a.first.data(), round1a.second.data(),
                                           0, 20, round1a.first.data());
    suite->shootout->sync();
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), 1);

    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> round1b =
        suite->shootout->getCurrentMatchPair();
    suite->shootout->onMatchResultReceived(round1b.second.data(), round1b.first.data(),
                                           1, 21, round1b.second.data());
    suite->shootout->sync();
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), 2)
    << "the round never advanced to the final";

    // Match 0's loser insists it won, tagged with match 0's index — so it is
    // recorded but does not end the final this device is watching.
    suite->shootout->onMatchResultReceived(round1a.second.data(), round1a.first.data(), 0, 22,
                                           round1a.second.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    ASSERT_TRUE(suite->shootout->isEliminated(round1a.first.data()));

    // The final's own result takes the other finalist out. Nobody is left.
    suite->shootout->onMatchResultReceived(round1a.first.data(), round1b.second.data(), 2, 23,
                                           round1a.first.data());
    suite->shootout->sync();

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a tournament with no survivor did not end with a reason";
    std::array<uint8_t, 6> noWinner{};
    EXPECT_EQ(memcmp(suite->shootout->getTournamentWinner().data(), noWinner.data(), 6), 0)
        << "an all-zero winner was published as a result";
}

// The sender, not the payload, decides whether a frame is ours. A neighbouring
// ring's coordinator can name devices that really are in our bracket, so content
// alone cannot tell its frames apart from our own coordinator's — and acting on
// them starts a match nobody here agreed to, or ends a tournament early.
inline void frameFromANonCoordinatorIsRefused(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> alienCoord = {0xA1, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    for (const std::array<uint8_t, 6>& m : {coord, me, other})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(coord.data(), {me, other, coord}, 1);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), -1);

    // Both payloads are entirely well-formed against our own bracket; only the
    // sender is wrong. Nothing may be acked either: an ack is a unicast, and a
    // unicast permanently registers its destination in the 20-entry ESP-NOW peer
    // table, so acking a stranger's ring spends that table on devices we will
    // never speak to again.
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kReliableAck, testing::_, testing::_))
    .Times(0);

    suite->shootout->onMatchStartReceived(alienCoord.data(), me.data(), other.data(), 0, 2);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), -1)
        << "a MATCH_START from a device that is not our coordinator started a match";

    suite->shootout->onTournamentEndReceived(alienCoord.data(), me.data(), 3);
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED)
        << "a TOURNAMENT_END from a device that is not our coordinator ended ours";
}

// Admission is the sender's, not the payload's. A frame from our coordinator is
// ours however its content reads, so a content fault is logged and still acked —
// dropping the ack leaves the coordinator retrying until it gives up.
inline void admittedFrameWithBadContentIsStillAcked(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> stranger = {0xA1, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    for (const std::array<uint8_t, 6>& m : {coord, me, other})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(coord.data(), {me, other, coord}, 1);

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kReliableAck, testing::_, testing::_))
    .Times(3)
    .WillRepeatedly(testing::Return(1));

    suite->shootout->onMatchStartReceived(coord.data(), me.data(), stranger.data(), 0, 2);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), -1)
        << "a pair naming a device outside the bracket started a match";

    suite->shootout->onMatchResultReceived(stranger.data(), other.data(), 0, 3, coord.data());
    EXPECT_FALSE(suite->shootout->isEliminated(other.data()))
        << "a result naming a device outside the bracket eliminated somebody";

    suite->shootout->onTournamentEndReceived(coord.data(), stranger.data(), 4);
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED)
        << "a winner outside the bracket ended the tournament";
}

// A cable nudge flickers the ring open for a tick or two. The abort guard is
// level triggered, so a break has to persist for RING_BREAK_DEBOUNCE_MS before it
// counts for anything.
inline void transientRingBreakDoesNotAbortATournament(ShootoutManagerTests* suite) {
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->closeRingOnJacks();
    suite->shootout->setRingMembersForTest({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});
    suite->shootout->startProposal();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    ASSERT_TRUE(suite->rdc.isInRing());

    suite->openRingOnJacks();
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "one tick off the ring ended a tournament";

    // Still broken, still inside the window. A literal rather than a fraction of
    // RING_BREAK_DEBOUNCE_MS: derived from the constant, this leg scales with it
    // and passes for any window at all, including none.
    suite->fakeClock->advance(250);
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "the guard fired before its window was up";

    // Healed inside the window: the guard has to forget what it saw, not carry a
    // part-spent window into the next break.
    suite->closeRingOnJacks();
    ASSERT_TRUE(suite->rdc.isInRing());
    suite->shootout->sync();
    suite->fakeClock->advance(2000);
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "a healed break still aborted once its original window elapsed";
}

// A settled break must abort even for a player mid-match, who is inside the match
// app with no shootout state mounted to notice.
inline void settledRingBreakAbortsALiveTournament(ShootoutManagerTests* suite) {
    suite->driveToFirstMatch({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    suite->openRingOnJacks();
    suite->shootout->sync();
    suite->fakeClock->advance(2000);
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED)
        << "a tournament outlived the ring it ran on";
}

inline void abortIsAckedByItsRecipient(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setRingMembersForTest({coord, me});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(coord.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kReliableAck, ackFor(PktType::kShootoutAbort),
                         sizeof(ReliableAckPayload)))
    .Times(1)
    .WillRepeatedly(testing::Return(1));

    suite->shootout->onAbortReceived(coord.data(), 7);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

// The phase test belongs inside the debounced condition, not around it. A device
// idling off a ring holds the break true from boot, so a guard that debounced the
// bare break and gated only the abort would arrive at its first tournament with
// the window already spent — and a member joins by radio, on the coordinator's
// RING_CLOSED, which can reach it before its own relay flag settles. Whatever
// grace that member got would be whatever was left of a window it never started.
inline void aFreshTournamentGetsAFullGraceWindow(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    // Idle, off any ring, for far longer than the window.
    ASSERT_FALSE(suite->rdc.isInRing());
    for (int i = 0; i < 10; ++i) {
        suite->fakeClock->advance(2000);
        suite->tick();
    }
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::IDLE);

    // Admitted to a tournament over the radio, with the local ring flag still
    // down — exactly the ordering a member sees.
    suite->shootout->onRingClosedReceived(coord.data(), {coord, me});
    suite->shootout->startProposal();
    ASSERT_FALSE(suite->rdc.isInRing());
    suite->shootout->sync();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "a tournament died on a window that expired before it existed";
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
    suite->shootout->setRingMembersForTest(members);

    std::vector<std::array<uint8_t, 6>> destinations;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutBracket, testing::_, testing::_))
    .WillByDefault(testing::Invoke([suite, &destinations](const uint8_t* dst, PktType type,
                                                          const uint8_t* data, const size_t len) {
        suite->recordFrame(type, data, len);
        std::array<uint8_t, 6> mac{};
        memcpy(mac.data(), dst, 6);
        destinations.push_back(mac);
        return 1;
    }));

    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    destinations.clear();  // drop the claim's own RING_CLOSED frame
    for (auto& m : members)
        suite->shootout->onConfirmReceived(m.data());

    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_EQ(suite->shootout->getBracket().size(), RING_SIZE);
    EXPECT_EQ(
    suite->pending(PktType::kShootoutBracket, suite->lastSeqId(PktType::kShootoutBracket)),
    RING_SIZE - 1);
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
    suite->shootout->setRingMembersForTest(members);

    suite->closeRingOnJacks();
    suite->shootout->onRingClosed();
    suite->shootout->startProposal();
    for (auto& m : members)
        suite->shootout->onConfirmReceived(m.data());
    ASSERT_EQ(
    suite->pending(PktType::kShootoutBracket, suite->lastSeqId(PktType::kShootoutBracket)), 3u);

    int sends = 0;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutBracket, testing::_, testing::_))
    .WillByDefault(testing::Invoke(
    [suite, &sends](const uint8_t*, PktType type, const uint8_t* data, const size_t len) {
        suite->recordFrame(type, data, len);
        sends++;
        return 1;
    }));

    // First backoff is Resender::backoffMs(0) = 100ms and every pending entry was
    // armed together, so one round covers all three.
    suite->fakeClock->advance(150);
    suite->tick();
    EXPECT_EQ(sends, 1);
    EXPECT_EQ(
    suite->pending(PktType::kShootoutBracket, suite->lastSeqId(PktType::kShootoutBracket)), 3u);
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
    suite->shootout->setRingMembersForTest({{0x0A, 0, 0, 0, 0, 0}, {0x0B, 0, 0, 0, 0, 0}});
    suite->shootout->startProposal();

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kReliableAck, testing::_, testing::_))
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

    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other});
    suite->shootout->startProposal();
    for (auto& m : {coord, me, other})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(coord.data(), {me, other, coord}, 1);
    suite->shootout->onMatchStartReceived(coord.data(), me.data(), other.data(), 0, 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), 0);

    suite->shootout->onMatchStartReceived(alienA.data(), alienA.data(), alienB.data(), 5, 3);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), 0);
    EXPECT_TRUE(suite->shootout->isInCurrentMatch());

    suite->shootout->onMatchResultReceived(alienA.data(), alienB.data(), 5, 4, alienA.data());
    EXPECT_FALSE(suite->shootout->isEliminated(alienB.data()));
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    suite->shootout->onTournamentEndReceived(alienA.data(), alienA.data(), 5);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    suite->shootout->onAbortReceived(alienA.data(), 0);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // A ring member's ABORT still lands.
    suite->shootout->onAbortReceived(coord.data(), 0);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

// The abort is one edge condition over ShootoutManager's phase, read by every state
// that can be interrupted by it. Builds the three apps and checks each of those
// edges against a hardcoded (state, edge index) table — it does not discover abort
// edges, so a state that gains one is invisible here; what it catches is a listed
// state losing its edge or ceasing to honour the condition, which on hardware is a
// ring that will not tear down.
//
// Note for anyone auditing #167 against this: the issue asked for abort to reach
// every shootout state through a shared guard and for the per-state edges to go
// away. The guard did move, further than #167 asked — out of the states entirely
// and into ShootoutManager::sync(), which is the only place a bracket player
// inside the match app can be reached. The edges are still declared per state:
// they carry the transition, and the manager carries the rule.
inline void abortRuleReachesEveryStateThatDeclaresIt(ShootoutManagerTests* suite) {
    GameContext ctx;
    ctx.shootoutManager = suite->shootout;

    HubApp hub(ctx);
    MatchApp matchApp(ctx);
    ShootoutApp shootoutApp(ctx);
    hub.populateStateMap();
    matchApp.populateStateMap();
    shootoutApp.populateStateMap();

    // (state id, index of that state's abort edge) for every state carrying one.
    // Positions are pinned independently by quickdrawAppEdgesMatchPreSplitGraph, so
    // an edge inserted ahead of one of these fails there too, not only here.
    const std::vector<std::pair<int, size_t>> abortEdges = {
    {IDLE, 3},
    {MATCH_COUNTDOWN, 0},
    {MATCH, 0},
    {MATCH_PUSHED, 0},
    {MATCH_RECEIVED_RESULT, 0},
    {MATCH_RESULT, 0},
    {SHOOTOUT_PROPOSAL, 1},
    {SHOOTOUT_BRACKET_REVEAL, 2},
    {SHOOTOUT_SPECTATOR, 2},
    {SHOOTOUT_ELIMINATED, 1},
    };

    std::map<int, State*> byId;
    for (StateMachine* app :
         {static_cast<StateMachine*>(&hub), static_cast<StateMachine*>(&matchApp),
          static_cast<StateMachine*>(&shootoutApp)}) {
        for (State* state : app->getStateMap())
            byId[state->getStateId()] = state;
    }

    // No tournament running: not one of them wants to leave for Aborted.
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::IDLE);
    for (const std::pair<int, size_t>& edge : abortEdges) {
        State* source = byId.count(edge.first) ? byId[edge.first] : nullptr;
        ASSERT_NE(source, nullptr) << "state " << edge.first << " missing";
        ASSERT_LT(edge.second, source->getTransitions().size());
        EXPECT_FALSE(source->getTransitions()[edge.second]->isConditionMet())
            << "state " << edge.first << " wants Aborted with no tournament running";
    }

    suite->shootout->setRingMembersForTest({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});
    suite->shootout->startProposal();
    suite->shootout->abortTournament();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);

    for (const std::pair<int, size_t>& edge : abortEdges) {
        EXPECT_TRUE(byId[edge.first]->getTransitions()[edge.second]->isConditionMet())
            << "state " << edge.first << " ignores the abort";
    }
}

inline void matchIndexAdvancesMonotonicallyAcrossRounds(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> b = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> c = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> d = {0x04, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));

    ShootoutMatchStartPayload lastMatchStart{};
    lastMatchStart.matchIndex = 0xFF;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, PktType::kShootoutMatchStart, testing::_, testing::_))
    .WillByDefault(testing::Invoke(
    [&lastMatchStart](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        if (len == sizeof(lastMatchStart)) memcpy(&lastMatchStart, data, len);
        return 1;
    }));

    suite->driveToFirstMatch({me, b, c, d});
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), 0);
    EXPECT_EQ(lastMatchStart.matchIndex, 0u);

    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> match0 =
    suite->shootout->getCurrentMatchPair();
    suite->shootout->onMatchResultReceived(match0.first.data(), match0.second.data(), 0, 10,
                                           match0.first.data());
    suite->shootout->sync();
    ASSERT_EQ(suite->shootout->getCurrentMatchIndex(), 1);
    EXPECT_EQ(lastMatchStart.matchIndex, 1u);

    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> match1 =
    suite->shootout->getCurrentMatchPair();
    suite->shootout->onMatchResultReceived(match1.first.data(), match1.second.data(), 1, 11,
                                           match1.first.data());
    suite->shootout->sync();

    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), 2);
    EXPECT_EQ(lastMatchStart.matchIndex, 2u);

    uint8_t followerMac[6] = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
    .WillByDefault(testing::Return(followerMac));
    suite->followRingHead(me);
    suite->shootout->setRingMembersForTest({me, b, c, d});
    suite->shootout->startProposal();
    for (const std::array<uint8_t, 6>& m : {me, b, c, d})
        suite->shootout->onConfirmReceived(m.data());
    suite->shootout->onBracketReceived(me.data(), {me, b, c, d}, 1);

    std::vector<uint8_t> resultSeqIds;
    suite->recordMatchResultSeqIds(&resultSeqIds);
    suite->shootout->onMatchStartReceived(me.data(), b.data(), c.data(), 0, 2);
    suite->shootout->reportLocalWin();
    ASSERT_FALSE(resultSeqIds.empty());
    const uint8_t round1Seq = resultSeqIds.back();
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
    ASSERT_NE(resultSeqIds.back(), round1Seq);

    suite->shootout->onMatchStartReceived(me.data(), b.data(), d.data(), 2, 3);
    suite->shootout->onMatchStartReceived(me.data(), me.data(), d.data(), 1, 4);
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), 2);
    suite->shootout->reportLocalWin();
    const uint8_t round2Seq = resultSeqIds.back();
    suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
    EXPECT_NE(resultSeqIds.back(), round2Seq)
    << "round 2's first match lost its result recovery to round 1's index";
}

inline void resultRetryIsReturnedByTournamentReset(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> other = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault(testing::Return(1));
    suite->followRingHead(coord);
    suite->shootout->setRingMembersForTest({coord, me, other});
    std::vector<uint8_t> resultSeqIds;
    suite->recordMatchResultSeqIds(&resultSeqIds);

    for (int tournament = 0; tournament < 2; ++tournament) {
        suite->shootout->startProposal();
        for (const std::array<uint8_t, 6>& m : {coord, me, other})
            suite->shootout->onConfirmReceived(m.data());
        suite->shootout->onBracketReceived(coord.data(), {coord, me, other}, 1);
        suite->shootout->onMatchStartReceived(coord.data(), me.data(), other.data(), 0, 2);
        suite->shootout->reportLocalWin();
        ASSERT_FALSE(resultSeqIds.empty());
        const uint8_t firstSeq = resultSeqIds.back();
        suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
        EXPECT_NE(resultSeqIds.back(), firstSeq)
        << "tournament " << tournament << " lost its result recovery to an earlier one";
        suite->runLoopFor(Resender::RETRANSMIT_SPAN_MS + 100);
    }
}
