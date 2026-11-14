#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <utility>
#include <vector>
#include "shootout-manager-tests.hpp"

// The states under test here are never ticked unless a test says so: what they
// know of the tournament has to reach them through the phase event.

/**
 * Lets a state draw. drawCenteredText chains off these, so a default nullptr
 * return dereferences on the first draw.
 */
inline void letTheDisplayDraw(ShootoutManagerTests* suite) {
    MockDisplay* display = suite->device.mockDisplay;
    ON_CALL(*display, invalidateScreen()).WillByDefault(testing::Return(display));
    ON_CALL(*display, setGlyphMode(testing::_)).WillByDefault(testing::Return(display));
    ON_CALL(*display, drawText(testing::_, testing::_, testing::_))
    .WillByDefault(testing::Return(display));
    ON_CALL(*display, getWidth()).WillByDefault(testing::Return(128));
    ON_CALL(*display, getTextWidth(testing::_)).WillByDefault(testing::Return(40));
}

/** Makes this device `selfMac` on the air. */
inline void standUpAs(ShootoutManagerTests* suite, uint8_t* selfMac) {
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(selfMac));
}

/**
 * A follower, 0x04, in a four-member tournament run by 0x01, holding the
 * bracket {0x02, 0x03, 0x01, 0x04} and waiting for the first match.
 */
struct FollowerInBracket {
    uint8_t selfMac[6] = {0x04, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> playerA = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> playerB = {0x03, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x04, 0, 0, 0, 0, 0};

    /** Drives `suite`'s manager to BRACKET_REVEAL as that follower. */
    explicit FollowerInBracket(ShootoutManagerTests* suite) {
        standUpAs(suite, selfMac);
        suite->followRingHead(coord);
        suite->shootout->onRingClosedReceived(coord.data(), {coord, playerA, playerB, me});
        suite->shootout->startProposal();
        for (const std::array<uint8_t, 6>& member : {coord, playerA, playerB, me}) {
            suite->shootout->onConfirmReceived(member.data());
        }
        suite->shootout->onBracketReceived(coord.data(), {playerA, playerB, coord, me}, 1);
    }
};

inline void phaseObserverHearsATournamentInOrder(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    standUpAs(suite, selfMac);
    std::vector<ShootoutManager::Phase> heard;
    suite->shootout->setOnPhaseEntered(
    [&heard](ShootoutManager::Phase phase) { heard.push_back(phase); }, &heard);

    suite->driveToFirstMatch({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});
    suite->shootout->reportLocalWin();
    suite->tick();

    const std::vector<ShootoutManager::Phase> expected = {
    ShootoutManager::Phase::PROPOSAL,
    ShootoutManager::Phase::BRACKET_REVEAL,
    ShootoutManager::Phase::MATCH_IN_PROGRESS,
    ShootoutManager::Phase::BETWEEN_MATCHES,
    ShootoutManager::Phase::ENDED,
    };
    EXPECT_TRUE(heard == expected) << "heard " << heard.size() << " phases";
    suite->shootout->setOnPhaseEntered(nullptr, &heard);
}

// An abort clears the tournament and then enters ABORTED. An observer told of
// an IDLE in between would take the tournament for over and the ring for free.
inline void abortIsHeardAsOnePhaseFromAnEmptiedManager(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    standUpAs(suite, selfMac);
    suite->driveToFirstMatch({{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}});

    std::vector<ShootoutManager::Phase> heard;
    size_t bracketSizeWhenTold = 99;
    suite->shootout->setOnPhaseEntered(
    [&heard, &bracketSizeWhenTold, suite](ShootoutManager::Phase phase) {
        heard.push_back(phase);
        bracketSizeWhenTold = suite->shootout->getBracket().size();
    },
    &heard);

    suite->shootout->abortTournament();

    ASSERT_EQ(heard.size(), 1u);
    EXPECT_EQ(heard[0], ShootoutManager::Phase::ABORTED);
    EXPECT_EQ(bracketSizeWhenTold, 0u) << "the observer was told before the teardown finished";
    suite->shootout->setOnPhaseEntered(nullptr, &heard);
}

// A state sent to the countdown by this event mounts a match on the next tick,
// so the match has to be primed by the time the event is heard.
inline void matchStartIsHeardOnceTheMatchIsPrimed(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x05, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> third = {0x07, 0, 0, 0, 0, 0};
    standUpAs(suite, selfMac);
    MockStorage storage;
    MatchManager matchManager(&suite->player, nullptr, &storage, nullptr, nullptr);
    suite->followRingHead(coord, &matchManager);
    suite->shootout->onRingClosedReceived(coord.data(), {me, coord, third});
    suite->shootout->startProposal();
    for (const std::array<uint8_t, 6>& member : {me, coord, third}) {
        suite->shootout->onConfirmReceived(member.data());
    }
    suite->shootout->onBracketReceived(coord.data(), {me, coord, third}, 1);

    bool toldOfTheMatch = false;
    bool primedWhenTold = false;
    suite->shootout->setOnPhaseEntered(
    [&](ShootoutManager::Phase phase) {
        if (phase != ShootoutManager::Phase::MATCH_IN_PROGRESS) return;
        toldOfTheMatch = true;
        primedWhenTold = matchManager.getCurrentMatch().has_value();
    },
    &toldOfTheMatch);

    suite->shootout->onMatchStartReceived(coord.data(), me.data(), coord.data(), 0, 2);

    EXPECT_TRUE(toldOfTheMatch);
    EXPECT_TRUE(primedWhenTold) << "the observer heard of a match nothing had primed yet";

    matchManager.clearCurrentMatch();
    suite->rebuildShootout(nullptr);
}

inline void proposalFollowsTheBracketOnlyWhileMounted(ShootoutManagerTests* suite) {
    uint8_t selfMac[6] = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> me = {0x02, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> coord = {0x01, 0, 0, 0, 0, 0};
    standUpAs(suite, selfMac);
    letTheDisplayDraw(suite);
    suite->followRingHead(coord);
    suite->shootout->onRingClosedReceived(coord.data(), {coord, me});

    GameContext ctx;
    ctx.shootoutManager = suite->shootout;
    ShootoutProposal state(ctx);
    state.onStateMounted(&suite->device);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::PROPOSAL);
    EXPECT_FALSE(state.transitionToBracketReveal());

    suite->shootout->onBracketReceived(coord.data(), {coord, me}, 1);
    EXPECT_TRUE(state.transitionToBracketReveal());

    state.onStateDismounted(&suite->device);
    EXPECT_FALSE(state.transitionToBracketReveal());

    // The next tournament's bracket arrives with this state off screen.
    suite->shootout->resetToIdle();
    suite->shootout->onBracketReceived(coord.data(), {coord, me}, 2);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_FALSE(state.transitionToBracketReveal())
    << "a dismounted state went on listening to the tournament";
}

inline void bracketRevealSendsPlayersAndSpectatorsDifferentWays(ShootoutManagerTests* suite) {
    FollowerInBracket follower(suite);
    letTheDisplayDraw(suite);
    GameContext ctx;
    ctx.shootoutManager = suite->shootout;
    ShootoutBracketReveal state(ctx);
    state.onStateMounted(&suite->device);
    EXPECT_FALSE(state.transitionToMatchCountdown());
    EXPECT_FALSE(state.transitionToSpectator());

    suite->shootout->onMatchStartReceived(follower.coord.data(), follower.playerA.data(),
                                          follower.playerB.data(), 0, 2);
    EXPECT_TRUE(state.transitionToSpectator());
    EXPECT_FALSE(state.transitionToMatchCountdown());

    suite->shootout->onMatchStartReceived(follower.coord.data(), follower.coord.data(),
                                          follower.me.data(), 1, 3);
    EXPECT_TRUE(state.transitionToMatchCountdown());
    EXPECT_FALSE(state.transitionToSpectator());

    state.onStateDismounted(&suite->device);
}

// A member that misses a MATCH_RESULT is still in MATCH_IN_PROGRESS when the
// next match starts. If that start names it, it has to be called up all the same.
inline void spectatorIsCalledUpByAMatchStartedOverAnUnresolvedOne(ShootoutManagerTests* suite) {
    FollowerInBracket follower(suite);
    letTheDisplayDraw(suite);
    suite->shootout->onMatchStartReceived(follower.coord.data(), follower.playerA.data(),
                                          follower.playerB.data(), 0, 2);
    GameContext ctx;
    ctx.shootoutManager = suite->shootout;
    ShootoutSpectator state(ctx);
    state.onStateMounted(&suite->device);
    ASSERT_FALSE(state.transitionToMatchCountdown());

    suite->shootout->onMatchStartReceived(follower.coord.data(), follower.coord.data(),
                                          follower.me.data(), 1, 3);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    EXPECT_TRUE(state.transitionToMatchCountdown());

    state.onStateDismounted(&suite->device);
}

inline void eliminatedLeavesForTheStandingsWhenTheTournamentEnds(ShootoutManagerTests* suite) {
    FollowerInBracket follower(suite);
    letTheDisplayDraw(suite);
    suite->shootout->onMatchStartReceived(follower.coord.data(), follower.coord.data(),
                                          follower.me.data(), 0, 2);
    suite->shootout->onMatchResultReceived(follower.coord.data(), follower.me.data(), 0, 3,
                                           follower.coord.data());
    GameContext ctx;
    ctx.shootoutManager = suite->shootout;
    ShootoutEliminated state(ctx);
    state.onStateMounted(&suite->device);
    EXPECT_FALSE(state.transitionToFinalStandings());

    suite->shootout->onTournamentEndReceived(follower.coord.data(), follower.coord.data(), 4);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::ENDED);
    EXPECT_TRUE(state.transitionToFinalStandings());

    // One mounted after the ending starts from it and does not wait for a change.
    ShootoutEliminated mountedLate(ctx);
    mountedLate.onStateMounted(&suite->device);
    EXPECT_TRUE(mountedLate.transitionToFinalStandings());

    mountedLate.onStateDismounted(&suite->device);
    state.onStateDismounted(&suite->device);
}

// Before the first match starts nothing has been played, so a result that turns
// up then is a stray. It is neither applied nor acknowledged.
inline void resultBeforeTheFirstMatchStartsIsIgnored(ShootoutManagerTests* suite) {
    FollowerInBracket follower(suite);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kReliableAck, ackFor(PktType::kShootoutMatchResult),
                         sizeof(ReliableAckPayload)))
    .Times(0);

    suite->shootout->onMatchResultReceived(follower.playerA.data(), follower.playerB.data(), 0, 7,
                                           follower.playerA.data());
    EXPECT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL);
    EXPECT_FALSE(suite->shootout->isEliminated(follower.playerB.data()));
    testing::Mock::VerifyAndClearExpectations(suite->device.mockPeerComms);

    // The same result is taken once the match it names has started.
    suite->shootout->onMatchStartReceived(follower.coord.data(), follower.playerA.data(),
                                          follower.playerB.data(), 0, 2);
    EXPECT_CALL(*suite->device.mockPeerComms,
                sendData(testing::_, PktType::kReliableAck, ackFor(PktType::kShootoutMatchResult),
                         sizeof(ReliableAckPayload)))
    .Times(1)
    .WillOnce(testing::Return(1));
    suite->shootout->onMatchResultReceived(follower.playerA.data(), follower.playerB.data(), 0, 7,
                                           follower.playerA.data());
    EXPECT_TRUE(suite->shootout->isEliminated(follower.playerB.data()));
}

/**
 * This device as coordinator of a four-member tournament, watching match 0
 * through a mounted spectator, with every screen render counted.
 */
struct WatchedFirstMatch {
    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    GameContext ctx;
    ShootoutSpectator* spectator = nullptr;
    int renders = 0;

    /** Drives to match 0, mounts the spectator, and counts renders from then on. */
    explicit WatchedFirstMatch(ShootoutManagerTests* suite) {
        standUpAs(suite, selfMac);
        letTheDisplayDraw(suite);
        ON_CALL(*suite->device.mockDisplay, render()).WillByDefault(testing::Invoke([this]() {
            renders++;
        }));
        suite->driveToFirstMatch({{0x01, 0, 0, 0, 0, 0},
                                  {0x02, 0, 0, 0, 0, 0},
                                  {0x03, 0, 0, 0, 0, 0},
                                  {0x04, 0, 0, 0, 0, 0}});
        ctx.shootoutManager = suite->shootout;
        spectator = new ShootoutSpectator(ctx);
        spectator->onStateMounted(&suite->device);
    }

    /** Frees the spectator, which closes its subscription. */
    ~WatchedFirstMatch() { delete spectator; }

    /** Match 0's result arrives and the coordinator starts match 1. */
    void finishFirstMatch(ShootoutManagerTests* suite) const {
        const std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> first =
        suite->shootout->getCurrentMatchPair();
        suite->shootout->onMatchResultReceived(first.first.data(), first.second.data(), 0, 9,
                                               first.first.data());
        suite->tick();
    }
};

inline void spectatorRepaintsOnceWhenTheNextMatchStarts(ShootoutManagerTests* suite) {
    WatchedFirstMatch watched(suite);
    ASSERT_EQ(watched.renders, 1) << "the mount paints the first pair";
    watched.spectator->onStateLoop(&suite->device);
    EXPECT_EQ(watched.renders, 1) << "nothing changed, so nothing to repaint";

    watched.finishFirstMatch(suite);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);
    watched.spectator->onStateLoop(&suite->device);
    EXPECT_EQ(watched.renders, 2) << "the new pair was not painted";
    watched.spectator->onStateLoop(&suite->device);
    EXPECT_EQ(watched.renders, 2);
}

// A teardown zeroes the current pair. A repaint still owed from the match that
// just started would put WATCHING over two blank names for the tick between the
// abort and the transition off this state.
inline void spectatorDoesNotRepaintATornDownMatch(ShootoutManagerTests* suite) {
    WatchedFirstMatch watched(suite);
    watched.finishFirstMatch(suite);
    ASSERT_EQ(suite->shootout->getPhase(), ShootoutManager::Phase::MATCH_IN_PROGRESS);

    suite->shootout->abortTournament();
    watched.spectator->onStateLoop(&suite->device);
    EXPECT_EQ(watched.renders, 1);
}

// A state deleted while mounted must not leave the manager a handler into it.
inline void phaseSubscriptionClosesItselfWhenDestroyed(ShootoutManagerTests* suite) {
    int calls = 0;
    {
        ShootoutPhaseSubscription subscription;
        subscription.open(suite->shootout, [&calls](ShootoutManager::Phase) { calls++; });
    }
    ASSERT_EQ(calls, 1) << "open hands over the present phase";
    suite->shootout->startProposal();
    EXPECT_EQ(calls, 1);
}
