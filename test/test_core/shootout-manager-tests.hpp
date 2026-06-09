#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/peer-graph-codec.hpp"
#include "game/shootout-manager.hpp"
#include "game/chain-manager.hpp"
#include "game/quickdraw-states.hpp"
#include "game/player.hpp"
#include "wireless/wireless-transport.hpp"

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
        // Single-device tests have no partner sending PROBEs, so production's
        // 2s silent-link threshold trips during multi-second time advances.
        // Push the threshold past anything any test cares to wait.
        rdc.setJackDeadSilentLinkMsForTest(60000);
        transport = new WirelessTransport(device.wirelessManager);
        chainManager = new ChainManager(&player, device.wirelessManager, &rdc);
        shootout = new ShootoutManager(&player, device.wirelessManager, &rdc, chainManager);
        shootout->initialize(transport);
    }

    void TearDown() override {
        delete shootout;
        shootout = nullptr;
        delete chainManager;
        chainManager = nullptr;
        delete transport;
        transport = nullptr;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // One platform-loop tick: game sync, then the loop's resender pump (the
    // platform loop owns transport->sync(); see main.cpp / cli-main.cpp).
    void syncShootout() {
        shootout->sync();
        transport->sync();
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    Player player{"TEST", Allegiance::RESISTANCE, true};
    WirelessTransport* transport = nullptr;
    ChainManager* chainManager = nullptr;
    ShootoutManager* shootout = nullptr;
    FakePlatformClock* fakeClock = nullptr;
    uint8_t localMac[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
};

// Deliver a full bracket the way the wire does: one BRACKET_ENTRY slot at a time
// through the transport's channel path (deliverIncoming -> channel.deliver ->
// onBracketEntryReceived), sent from the coordinator (the lowest MAC in the
// bracket, matching how a follower anchors coordinatorMac_). Exercises the real
// reassembly instead of a synthetic whole-bracket injection.
inline void deliverBracket(ShootoutManagerTests* suite,
                           const std::vector<std::array<uint8_t, 6>>& bracket,
                           uint8_t batchId) {
    std::array<uint8_t, 6> coord = bracket.front();
    for (const auto& m : bracket) {
        if (memcmp(m.data(), coord.data(), 6) < 0) coord = m;
    }
    for (size_t i = 0; i < bracket.size(); ++i) {
        ShootoutBracketEntryPayload entry{};
        entry.cmd = static_cast<uint8_t>(ShootoutCmd::BRACKET_ENTRY);
        entry.seqId = static_cast<uint8_t>(i + 1);
        entry.batchId = batchId;
        entry.slot = static_cast<uint8_t>(i);
        entry.totalSlots = static_cast<uint8_t>(bracket.size());
        memcpy(entry.mac, bracket[i].data(), 6);
        suite->transport->deliverIncoming(
            PktType::kShootoutCommand,
            static_cast<uint8_t>(ShootoutCmd::BRACKET_ENTRY),
            coord.data(),
            reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));
    }
}

// A bracket larger than the ESP-NOW peer cap (MAX_SHOOTOUT_MEMBERS) cannot be
// fanned out without dropping members, so the coordinator must abort rather than
// silently register only some. Drives confirmedSet_ past the cap and asserts the
// tournament aborts on bracket send.
inline void oversizedBracketAbortsTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    // One more member than the cap. Self (0x01) is lowest -> coordinator.
    std::vector<std::array<uint8_t, 6>> members;
    for (int i = 1; i <= MAX_SHOOTOUT_MEMBERS + 1; ++i) {
        members.push_back({static_cast<uint8_t>(i), 0, 0, 0, 0, 0});
    }
    suite->shootout->setLoopMembersForTest(members);
    suite->shootout->startProposal();
    for (const auto& m : members) {
        if (memcmp(m.data(), selfMac, 6) == 0) continue;
        suite->shootout->onConfirmReceived(m.data());
    }
    // Final (self) confirm completes the gather and triggers bracket generation,
    // which finds the bracket oversized and aborts.
    suite->shootout->confirmLocal();

    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

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

inline void confirmRetriesUntilAckedDuringProposal(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    suite->shootout->setLoopMembersForTest({
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}
    });
    // The peer never acks, so the reliable CONFIRM stays pending and the
    // Resender re-sends it on its retry schedule. Redelivery is owned by the
    // transport, not a phase-gated rebroadcast: a single dropped CONFIRM no
    // longer strands the coordinator's gather.
    std::atomic<int> sendCount{0};
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [&sendCount](const uint8_t*, PktType, const uint8_t*, const size_t) {
                sendCount.fetch_add(1);
                return 1;
            }));
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    int afterInitial = sendCount.load();
    ASSERT_GE(afterInitial, 1);

    // Advance past the first retry interval (ackTimeoutForRetry(0)=100ms) and
    // drive the transport. A retry must re-send CONFIRM.
    suite->fakeClock->advance(200);
    suite->syncShootout();
    EXPECT_GT(sendCount.load(), afterInitial);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
}

inline void nonCoordinatorDoesNotAdvanceOnConfirmTally(ShootoutManagerTests* suite) {
    // Self is not the lowest MAC, so it is a non-coordinator. Even if it tallies
    // every member's CONFIRM, only the coordinator advances to BRACKET_REVEAL;
    // a non-coordinator waits for BRACKET_ENTRY. Guards the single-authority
    // rule that closed the confirm-gather race.
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    std::array<uint8_t, 6> low = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me  = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> hi  = {0x07, 0, 0, 0, 0, 0};
    suite->shootout->setLoopMembersForTest({low, me, hi});  // self not lowest
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(low.data());
    suite->shootout->onConfirmReceived(hi.data());
    EXPECT_FALSE(suite->shootout->isCoordinator());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
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
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(m2.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    suite->shootout->onConfirmReceived(m3.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
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
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::BRACKET_ENTRY), 2u);
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, members[1].data());
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::BRACKET_ENTRY), 1u);
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, members[2].data());
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::BRACKET_ENTRY), 0u);
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
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    for (int i = 0; i < 60; i++) {
        suite->fakeClock->advance(100);
        suite->syncShootout();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

// Coordinator broadcasts MATCH_START to the bracket; if a peer never ACKs
// within the Resender retry budget, the tournament aborts.
inline void matchStartAbandonAbortsTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, opMac.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // Never ack MATCH_START. Drive enough sync ticks to exhaust Resender's
    // exponential backoff (100, 200, 400, ~1600ms ceiling on the abandon).
    for (int i = 0; i < 60; i++) {
        suite->fakeClock->advance(100);
        suite->syncShootout();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

// A duelist sends MATCH_RESULT; if no one ACKs, the tournament
// aborts. Without this, a lost MATCH_RESULT would hang the tournament.
inline void matchResultAbandonAbortsTournament(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({me, coord});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(coord.data());
    deliverBracket(suite, {coord, me}, 1);
    suite->shootout->onMatchStartReceived(me.data(), coord.data(), 0, 2);
    suite->shootout->reportLocalWin();

    for (int i = 0; i < 60; i++) {
        suite->fakeClock->advance(100);
        suite->syncShootout();
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

    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();

    auto bracket = suite->shootout->getBracket();

    // Ack from only one peer. Reveal window expires; MATCH_START must NOT fire.
    for (const auto& m : bracket) {
        if (memcmp(m.data(), selfMac, 6) != 0) {
            suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, m.data());
            break;
        }
    }
    suite->fakeClock->advance(6000);
    suite->syncShootout();
    EXPECT_NE(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    // Ack the remaining peers. Now MATCH_START fires on next sync.
    for (const auto& m : bracket) {
        if (memcmp(m.data(), selfMac, 6) == 0) continue;
        suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, m.data());
    }
    suite->syncShootout();
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
    deliverBracket(suite, {me, coord, other}, 1);
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

inline void lowerMacBracketEntryStandsDownAndAdoptsPostBracket(ShootoutManagerTests* suite) {
    // Two rings closed independently and merged: self already self-elected,
    // generated, and anchored its own bracket (coordinatorMac_ == self). A
    // BRACKET_ENTRY then arrives from a strictly-lower-MAC coordinator. Self
    // must stand down (drop its coordinator anchor and bracket) and adopt the
    // lower-MAC bracket, not keep running its own (which would split-brain the
    // tournament).
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    std::array<uint8_t, 6> me = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> p6 = {0x06, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> p7 = {0x07, 0, 0, 0, 0, 0};
    suite->shootout->setLoopMembersForTest({me, p6, p7});  // self lowest -> coordinator
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(p6.data());
    suite->shootout->onConfirmReceived(p7.data());

    // Self built and anchored its own bracket.
    ASSERT_TRUE(suite->shootout->isCoordinator());
    ASSERT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), me.data(), 6), 0);

    // A lower-MAC coordinator's single-slot bracket arrives over the real
    // channel path (deliverIncoming -> channel.deliver -> onBracketEntryReceived).
    std::array<uint8_t, 6> lowerCoord = {0x02, 0, 0, 0, 0, 0};
    ShootoutBracketEntryPayload entry{};
    entry.cmd = static_cast<uint8_t>(ShootoutCmd::BRACKET_ENTRY);
    entry.seqId = 1;
    entry.batchId = 1;
    entry.slot = 0;
    entry.totalSlots = 1;
    memcpy(entry.mac, lowerCoord.data(), 6);
    suite->transport->deliverIncoming(
        PktType::kShootoutCommand,
        static_cast<uint8_t>(ShootoutCmd::BRACKET_ENTRY),
        lowerCoord.data(),
        reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));

    EXPECT_FALSE(suite->shootout->isCoordinator());
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), lowerCoord.data(), 6), 0);
    auto adopted = suite->shootout->getBracket();
    ASSERT_EQ(adopted.size(), 1u);
    EXPECT_EQ(memcmp(adopted[0].data(), lowerCoord.data(), 6), 0);
}

inline void higherMacBracketEntryDoesNotUnseatCoordinator(ShootoutManagerTests* suite) {
    // Mirror image: a BRACKET_ENTRY from a HIGHER MAC must not unseat the
    // rightful lower-MAC coordinator (the higher peer should adopt ours).
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> p3 = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> p4 = {0x04, 0, 0, 0, 0, 0};
    suite->shootout->setLoopMembersForTest({me, p3, p4});  // self lowest -> coordinator
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    suite->shootout->onConfirmReceived(p3.data());
    suite->shootout->onConfirmReceived(p4.data());
    ASSERT_TRUE(suite->shootout->isCoordinator());

    std::array<uint8_t, 6> higher = {0x09, 0, 0, 0, 0, 0};
    ShootoutBracketEntryPayload entry{};
    entry.cmd = static_cast<uint8_t>(ShootoutCmd::BRACKET_ENTRY);
    entry.seqId = 1;
    entry.batchId = 1;
    entry.slot = 0;
    entry.totalSlots = 1;
    memcpy(entry.mac, higher.data(), 6);
    suite->transport->deliverIncoming(
        PktType::kShootoutCommand,
        static_cast<uint8_t>(ShootoutCmd::BRACKET_ENTRY),
        higher.data(),
        reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));

    EXPECT_TRUE(suite->shootout->isCoordinator());
    EXPECT_EQ(memcmp(suite->shootout->getCoordinatorMac().data(), me.data(), 6), 0);
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
    deliverBracket(suite, {me, opMac, coord}, 1);
    suite->shootout->onMatchStartReceived(me.data(), opMac.data(), 0, 2);

    suite->shootout->reportLocalWin();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BETWEEN_MATCHES);
    EXPECT_TRUE(suite->shootout->isEliminated(opMac.data()));
    EXPECT_FALSE(suite->shootout->isEliminated(me.data()));
}

// A non-coordinator winner must still transmit MATCH_RESULT to the roster.
// Non-coordinators only ever confirm TO the coordinator, so their confirmedSet_
// holds just themselves; the result therefore has to target the bracket (the
// full roster every device learns via BRACKET_ENTRY), not confirmedSet_. If it
// targets confirmedSet_ the send reaches nobody, the coordinator never learns
// the outcome, and the tournament stalls after the match.
inline void nonCoordinatorWinnerBroadcastsMatchResult(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({coord, me, opMac});

    // Realistic non-coordinator path: it confirms locally (self only) and never
    // receives peer CONFIRMs (those are addressed to the coordinator). Receiving
    // the bracket must adopt the full roster, otherwise the win below reaches
    // nobody.
    suite->shootout->startProposal();
    suite->shootout->confirmLocal();
    EXPECT_EQ(suite->shootout->getConfirmedCount(), 1u) << "follower should start self-only";
    deliverBracket(suite, {coord, me, opMac}, 1);
    EXPECT_EQ(suite->shootout->getBracket().size(), 3u) << "bracket not reassembled";
    EXPECT_EQ(suite->shootout->getConfirmedCount(), 3u) << "follower must adopt the bracket roster";
    suite->shootout->onMatchStartReceived(me.data(), opMac.data(), 0, 2);

    suite->shootout->reportLocalWin();

    // Two peers besides self (coord + opponent) must each get a reliable
    // MATCH_RESULT awaiting ack. Zero means it was sent to nobody and the
    // coordinator would never advance the bracket.
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::MATCH_RESULT), 2u);
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
    deliverBracket(suite, {aMac, bMac, coord, me}, 1);
    suite->shootout->onMatchStartReceived(aMac.data(), bMac.data(), 0, 2);
    suite->shootout->onMatchResultReceived(aMac.data(), bMac.data(), 0, 3, aMac.data());
    EXPECT_TRUE(suite->shootout->isEliminated(bMac.data()));
    EXPECT_FALSE(suite->shootout->isEliminated(aMac.data()));
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
    deliverBracket(suite, {me, other, coord}, 1);
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
    deliverBracket(suite, {me, other, coord}, 1);
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
    suite->shootout->startProposal();
    for (auto& m : std::vector<std::array<uint8_t,6>>{me, a, b, c}) {
        suite->shootout->onConfirmReceived(m.data());
    }
    // Self is coord, so the bracket was generated during the confirm flow.
    // Ack every non-self entry to drive reveal → MATCH_START.
    for (const auto& m : suite->shootout->getBracket()) {
        if (memcmp(m.data(), me.data(), 6) != 0) {
            suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, m.data());
        }
    }
    suite->fakeClock->advance(6000);
    suite->syncShootout();  // fires match 0

    auto pair = suite->shootout->getCurrentMatchPair();
    // Pick any bracket member not in the current pair as the spectator to lose.
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
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    // Self is coord; bracket is already set. Ack bracket from peer.
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, opMac.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();  // fires MATCH_START 0
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_START, opMac.data());
    // Coordinator wins the only match: no more pairs, so maybeStartNextMatch
    // broadcasts TOURNAMENT_END and ends.
    suite->shootout->reportLocalWin();
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
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, opMac.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_START, opMac.data());
    suite->shootout->reportLocalWin();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    ASSERT_FALSE(suite->shootout->getBracket().empty());
    ASSERT_TRUE(suite->shootout->isEliminated(opMac.data()));
    ASSERT_GE(suite->shootout->getCurrentMatchIndex(), 0);

    // Starting a fresh tournament must reset bracket_, eliminated_,
    // currentMatchIndex_, and pending-ack tracking from the prior run.
    suite->shootout->startProposal();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_TRUE(suite->shootout->getBracket().empty());
    EXPECT_EQ(suite->shootout->getConfirmedCount(), 0u);
    EXPECT_FALSE(suite->shootout->isEliminated(opMac.data()));
    EXPECT_EQ(suite->shootout->getCurrentMatchIndex(), -1);
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::BRACKET_ENTRY), 0u);
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
    suite->shootout->startProposal();
    for (auto& m : {me, b, c, d}) suite->shootout->onConfirmReceived(m.data());
    for (auto& m : {b, c, d}) suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, m.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();
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

// A stray ABORT packet arriving after phase has moved to ENDED must not
// demote the tournament. Two devices can diverge (one already ENDED, the
// other still mid-tournament and emitting ABORT for unrelated reasons);
// the ENDED device must keep its winner display.
inline void abortReceivedAfterTournamentEndStaysEnded(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> winner = {0x03, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    suite->shootout->setLoopMembersForTest({me, coord, winner});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(coord.data());
    suite->shootout->onConfirmReceived(winner.data());
    deliverBracket(suite, {coord, me, winner}, 1);
    suite->shootout->onTournamentEndReceived(winner.data(), 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);

    // Stray ABORT must not demote ENDED.
    suite->shootout->onAbortReceived();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    EXPECT_EQ(suite->shootout->getTournamentWinner(), winner);
}

// On the win-the-final-match path, reportLocalWin sends MATCH_RESULT then
// maybeStartNextMatch transitions phase=ENDED, leaving an in-flight reliable.
// If it abandons, the latch must not flip the tournament back to ABORTED.
inline void matchResultAbandonAfterTournamentEndStaysEnded(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));
    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, opMac.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_START, opMac.data());

    suite->shootout->reportLocalWin();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    // The ENDED transition must drop in-flight MATCH_RESULT pending so a
    // straggling abandon cannot latch tournamentAbortPending_.
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::MATCH_RESULT), 0u);

    // Never ack MATCH_RESULT; advance long enough to exhaust the retry budget.
    for (int i = 0; i < 60; i++) {
        suite->fakeClock->advance(100);
        suite->syncShootout();
    }
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
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
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, opMac.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_START, opMac.data());
    suite->shootout->reportLocalWin();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    ASSERT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::TOURNAMENT_END), 1u);

    // Snapshot send count immediately after the initial TOURNAMENT_END
    // broadcast — anything further must come from the retry path.
    int sendCountAfterInitialBroadcast = sendCount.load();

    // Advance past the first retry interval (ackTimeoutForRetry(0)=100ms) and
    // drive sync(). A retry must re-broadcast TOURNAMENT_END.
    suite->fakeClock->advance(200);
    suite->syncShootout();
    EXPECT_GT(sendCount.load(), sendCountAfterInitialBroadcast);
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::TOURNAMENT_END), 1u);

    // Correct ack clears the pending entry.
    suite->shootout->cancelPendingForTest(ShootoutCmd::TOURNAMENT_END, opMac.data());
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::TOURNAMENT_END), 0u);
}

// ABORT is a tournament-terminal command like TOURNAMENT_END: a peer that
// misses it sits in a dead tournament until disconnect detection or its own
// retry exhaustion bails it out, so it gets the same ack+retry treatment.
// The retries must SURVIVE the resetToIdle that follows the broadcast inside
// abortTournament; the reset cancels the other channels' pending sends for
// every bracket target, and ABORT's slot must stay armed through it.
inline void abortRetriesUntilAcked(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};
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

    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, opMac.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();

    suite->shootout->abortTournament();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    // (pendingAckCount can't observe this slot: it counts against
    // tournament_.bracket, which abortTournament's resetToIdle just wiped.
    // The retransmit itself is the observable.)
    int sendCountAfterInitialBroadcast = sendCount.load();

    // Past the first retry interval, sync() must re-send the un-acked ABORT;
    // the pending slot survived resetToIdle.
    suite->fakeClock->advance(200);
    suite->syncShootout();
    EXPECT_GT(sendCount.load(), sendCountAfterInitialBroadcast);

    // Ack clears it: no further retransmits.
    suite->shootout->cancelPendingForTest(ShootoutCmd::ABORT, opMac.data());
    int sendCountAfterAck = sendCount.load();
    suite->fakeClock->advance(500);
    suite->syncShootout();
    EXPECT_EQ(sendCount.load(), sendCountAfterAck);
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
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    suite->shootout->onConfirmReceived(spec.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, opMac.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, spec.data());
    suite->fakeClock->advance(6000);
    suite->syncShootout();
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_START, opMac.data());
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_START, spec.data());

    // Self wins match 0 → broadcasts MATCH_RESULT to opMac and spec.
    suite->shootout->reportLocalWin();
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::MATCH_RESULT), 2u);

    int sendCountAfterInitial = sendCount.load();

    // After ackTimeoutForRetry(0)=100ms, sync() retries to both pending peers.
    suite->fakeClock->advance(200);
    suite->syncShootout();
    EXPECT_GT(sendCount.load(), sendCountAfterInitial);
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::MATCH_RESULT), 2u);

    // Acks clear pending.
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_RESULT, opMac.data());
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::MATCH_RESULT), 1u);
    suite->shootout->cancelPendingForTest(ShootoutCmd::MATCH_RESULT, spec.data());
    EXPECT_EQ(suite->shootout->pendingAckCount(ShootoutCmd::MATCH_RESULT), 0u);
}

// The shootout assigns per-match draw-slots through the match (MAC-ordered, see
// matchManagerShootoutMatchRoleDecoupledFromGlobalRole), never by mutating the
// shared Player's hunter/bounty role. Driving a real match-start through a wired
// MatchManager must set the per-match slot on the MATCH while leaving
// player.isHunter() untouched, so the chain layer that reads the global role is
// never churned and never re-floods a role. Root-cause guard for the former
// role-flip → chain-cascade coupling.
inline void shootoutDoesNotMutateGlobalRole(ShootoutManagerTests* suite) {
    suite->chainManager->initialize(suite->transport);  // production wiring: chain role-changed callback live

    // Wire a real MatchManager so primeMatchManagerForMatch actually runs (the
    // bare fixture leaves it null, early-returning before any role logic).
    // No transport: shootout match priming sends nothing on the quickdraw
    // channel, and the fixture transport's quickdraw slot stays unclaimed.
    MockStorage storage;
    MatchManager mm;
    mm.initialize(&suite->player, &storage, nullptr);
    mm.setRemoteDeviceCoordinator(&suite->rdc);
    suite->shootout->setMatchManager(&mm);

    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me    = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> opMac = {0x02, 0, 0, 0, 0, 0};  // < self → MAC-ordered bounty slot
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));

    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, testing::_, testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(1));

    const bool original = suite->player.isHunter();

    suite->shootout->setLoopMembersForTest({me, opMac});
    suite->shootout->startProposal();
    suite->shootout->onConfirmReceived(me.data());
    suite->shootout->onConfirmReceived(opMac.data());
    deliverBracket(suite, {me, opMac}, 1);
    suite->shootout->onMatchStartReceived(me.data(), opMac.data(), 0, 2);

    // primeMatchManagerForMatch ran: the per-match slot lives on the MATCH
    // (MAC-ordered: self 0x05 > opp 0x02 → bounty) while the global role is
    // untouched. Role rides the BEACON flood off player.isHunter(), so an
    // unchanged global role is exactly what keeps the chain layer from
    // re-flooding a role.
    ASSERT_TRUE(mm.getCurrentMatch().has_value());
    EXPECT_FALSE(mm.localIsHunterForMatch());       // MAC-ordered bounty slot
    EXPECT_EQ(suite->player.isHunter(), original);  // global role untouched

    suite->shootout->setMatchManager(nullptr);  // detach local mm before teardown
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
    suite->shootout->startProposal();
    for (auto& m : std::vector<std::array<uint8_t,6>>{me, a, b, c}) {
        suite->shootout->onConfirmReceived(m.data());
    }
    for (const auto& m : suite->shootout->getBracket()) {
        if (memcmp(m.data(), me.data(), 6) != 0) {
            suite->shootout->cancelPendingForTest(ShootoutCmd::BRACKET_ENTRY, m.data());
        }
    }
    suite->fakeClock->advance(6000);
    suite->syncShootout();  // start match 0

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

// ShootoutProposal must NOT exit to Idle on a single-tick coordinator-
// demote blip. Cable nudges can flip the ChainManager coordinator flag for one loop
// iteration, and naive code would wipe tournament state on every tick that
// reads false. Debounce requires the loss to persist for
// kLoopBreakDebounceMs before treating it as a real break.
inline void shootoutProposalDebouncesTransientLoopBreak(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    ON_CALL(*suite->device.mockPeerComms,
            sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(1));

    FakeChainManager fakeChain(&suite->player, suite->device.wirelessManager, &suite->rdc);
    // Loss is driven by the settled-topology signal the production code reads:
    // a ring member leaves the proposal when the topology has SETTLED into a
    // non-loop. inStableLoop=true means "ring intact"; false (with the topology
    // still stable) means "ring settled open" → abort.
    fakeChain.setInStableLoop(true);
    suite->shootout->setLoopMembersForTest({{0x01,0,0,0,0,0}, {0x02,0,0,0,0,0}});
    suite->shootout->startProposal();
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);

    ShootoutProposal state(suite->shootout, &fakeChain);

    // Single-tick blip: phase must remain PROPOSAL.
    fakeChain.setInStableLoop(false);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_FALSE(state.transitionToAborted());

    // Loop returns within debounce window — debounce cleared, phase intact.
    fakeChain.setInStableLoop(true);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_FALSE(state.transitionToAborted());

    // Persistent loss past the debounce window: abort fires, routing every
    // ring member through the Aborted screen (phase=ABORTED) before idle.
    fakeChain.setInStableLoop(false);
    state.onStateLoop(nullptr);  // start debounce
    suite->fakeClock->advance(2000);  // well past any reasonable debounce window
    state.onStateLoop(nullptr);  // debounce elapses → abortTournament() → ABORTED
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    // The phase==ABORTED check sits at the top of onStateLoop, ahead of the
    // debounce that flips the phase, so shouldGoToAborted_ latches on the next
    // tick (a harmless ~1ms lag in production; the SM re-checks every tick).
    state.onStateLoop(nullptr);
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

    FakeChainManager fakeChain(&suite->player, suite->device.wirelessManager, &suite->rdc);
    fakeChain.setInStableLoop(true);
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01,0,0,0,0,0}, {0x02,0,0,0,0,0}, {0x03,0,0,0,0,0}
    };
    suite->shootout->setLoopMembersForTest(members);
    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);

    ShootoutBracketReveal state(suite->shootout, &fakeChain);

    fakeChain.setInStableLoop(false);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_FALSE(state.transitionToAborted());

    fakeChain.setInStableLoop(true);
    state.onStateLoop(nullptr);
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_FALSE(state.transitionToAborted());

    fakeChain.setInStableLoop(false);
    state.onStateLoop(nullptr);
    suite->fakeClock->advance(2000);
    state.onStateLoop(nullptr);  // debounce elapses → abortTournament() → ABORTED
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    state.onStateLoop(nullptr);  // next tick latches shouldGoToAborted_
    EXPECT_TRUE(state.transitionToAborted());
}

// buildLoopMemberSet returns empty while the topology is still settling:
// consumers must not derive bracket state from mid-convergence snapshots.
inline void shootoutBuildLoopMemberSetEmptyWhenTopologyUnstable(ShootoutManagerTests* suite) {
    // A freshly formed mutual edge (HELLO self-claims the peer, the peer's BEACON
    // claims us back) restamps the stability clock, so isTopologyStable is false
    // for the next 200ms; buildLoopMemberSet must withhold members until then.
    // A HELLO alone is a one-sided claim, not yet an edge, so it would NOT make
    // the graph unstable; the reciprocating BEACON is what does.
    net::Mac peer = {0x33, 0x33, 0x33, 0x33, 0x33, 0xA1};
    net::Mac self;
    std::copy_n(suite->localMac, 6, self.begin());
    ASSERT_NE(suite->device.outputJackSerial.bytesCallback, nullptr);
    auto hello = peer_graph::encodeHello(peer, static_cast<uint8_t>(DeviceType::PDN), peer_graph::kUserIdUnknown);
    suite->device.outputJackSerial.bytesCallback(hello.data(), hello.size());
    suite->rdc.sync();
    BeaconRecord pb{peer, self, {}};  // peer claims self on its IN → mutual edge
    auto beacon = peer_graph::encodeBeacon(pb);
    suite->device.outputJackSerial.bytesCallback(beacon.data(), beacon.size());
    suite->rdc.sync();  // edge forms now; window has not elapsed
    ASSERT_FALSE(suite->rdc.isTopologyStable());
    auto members = suite->shootout->getLoopMembers();
    EXPECT_TRUE(members.empty())
        << "buildLoopMemberSet must return empty until isTopologyStable becomes true";
}

// buildLoopMemberSet returns rdc->getChainMembers() once stability is reached.
inline void shootoutBuildLoopMemberSetReturnsChainMembersWhenStable(ShootoutManagerTests* suite) {
    // Inject a peer's HELLO (sets macPeer → self claims it) and the peer's
    // BEACON claiming self back, forming a mutual edge so the peer becomes a
    // chain member. Then advance past the 200ms stability window.
    net::Mac peer = {0x33, 0x33, 0x33, 0x33, 0x33, 0xA1};
    net::Mac self;
    std::copy_n(suite->localMac, 6, self.begin());
    ASSERT_NE(suite->device.outputJackSerial.bytesCallback, nullptr);
    auto hello = peer_graph::encodeHello(peer, static_cast<uint8_t>(DeviceType::PDN), peer_graph::kUserIdUnknown);
    suite->device.outputJackSerial.bytesCallback(hello.data(), hello.size());
    suite->rdc.sync();
    BeaconRecord pb{peer, self, {}};  // peer claims self on its IN
    auto beacon = peer_graph::encodeBeacon(pb);
    suite->device.outputJackSerial.bytesCallback(beacon.data(), beacon.size());
    suite->rdc.sync();

    suite->fakeClock->advance(250); suite->rdc.sync();
    ASSERT_TRUE(suite->rdc.isTopologyStable());

    auto members = suite->shootout->getLoopMembers();
    auto expected = suite->rdc.getChainMembers();
    EXPECT_FALSE(members.empty());
    EXPECT_EQ(members.size(), expected.size());
    // Membership equality: every expected MAC appears in members.
    for (const auto& e : expected) {
        bool found = false;
        for (const auto& m : members) {
            if (memcmp(m.data(), e.data(), 6) == 0) { found = true; break; }
        }
        EXPECT_TRUE(found) << "expected MAC missing from buildLoopMemberSet output";
    }
}
