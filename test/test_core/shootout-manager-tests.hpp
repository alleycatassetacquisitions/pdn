#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/shootout-manager.hpp"
#include "game/chain-duel-manager.hpp"
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
        cdm = std::make_unique<ChainDuelManager>(&player, device.wirelessManager, &rdc);
        shootout = std::make_unique<ShootoutManager>(&player, device.wirelessManager, &rdc, cdm.get());
    }

    void TearDown() override {
        shootout.reset();
        cdm.reset();
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    Player player{"TEST", Allegiance::RESISTANCE, true};
    std::unique_ptr<ChainDuelManager> cdm;
    std::unique_ptr<ShootoutManager> shootout;
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

inline void coordinatorIsLowestMacAmongConfirmed(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};  // self is the lowest → coordinator
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
    auto coord = suite->shootout->getCoordinatorMac();
    EXPECT_EQ(memcmp(coord.data(), b.data(), 6), 0);
    EXPECT_TRUE(suite->shootout->isCoordinator());
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

inline void coordinatorBroadcastsBracketOnAdvance(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    std::vector<std::array<uint8_t, 6>> members = {
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}
    };
    suite->shootout->setLoopMembersForTest(members);

    EXPECT_CALL(*suite->device.mockPeerComms,
        sendData(testing::_, PktType::kShootoutCommand, testing::_, testing::_))
        .Times(testing::AtLeast(2))
        .WillRepeatedly(testing::Return(1));

    suite->shootout->startProposal();
    for (auto& m : members) suite->shootout->onConfirmReceived(m.data());
    suite->shootout->confirmLocal();
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_EQ(suite->shootout->getBracketPendingAckCount(), 2u);
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
    suite->shootout->onBracketReceived({me, coord, other}, 1);
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
    suite->shootout->onBracketReceived({me, opMac, coord}, 1);
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
    suite->shootout->onBracketReceived({aMac, bMac, coord, me}, 1);
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
    suite->shootout->onBracketReceived({me, other, coord}, 1);
    suite->shootout->onMatchStartReceived(me.data(), other.data(), 0, 2);
    suite->shootout->onPeerLostReceived(coord.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
}

inline void peerLostActiveDuelistOpponentWins(ShootoutManagerTests* suite) {
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
    suite->shootout->onBracketReceived({me, other, coord}, 1);
    suite->shootout->onMatchStartReceived(me.data(), other.data(), 0, 2);
    suite->shootout->onPeerLostReceived(other.data());
    EXPECT_TRUE(suite->shootout->isEliminated(other.data()));
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BETWEEN_MATCHES);
}

inline void peerLostSpectatorMarksForfeit(ShootoutManagerTests* suite) {
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
    EXPECT_TRUE(suite->shootout->isForfeited(spectator.data()));
    // Current match unaffected
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
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
    suite->shootout->onBracketReceived({me, opMac, third}, 1);
    suite->shootout->onMatchStartReceived(me.data(), opMac.data(), 0, 2);
    suite->player.setIsHunter(!originalIsHunter);
    suite->shootout->onPeerLostReceived(opMac.data());
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ABORTED);
    suite->shootout->resetToIdle();
    EXPECT_EQ(suite->player.isHunter(), originalIsHunter);
}
