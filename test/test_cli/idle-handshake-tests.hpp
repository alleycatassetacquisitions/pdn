#pragma once

// A bounty takes a hunter's match handshake only on its idle screen (spec
// quickdraw-match: "an idle Bounty challenger"). Elsewhere it cannot play, so a
// hunter that plugs in must not be counted down, given a loss, or store a duel
// the bounty never saw (#527).

#include <gtest/gtest.h>

#include <functional>

#include "native-driver-tests.hpp"

// Hunter 0 and bounty 1 fight the first duel while hunter 2 waits idle and
// uncabled. Time moves 5 ms a tick and every player presses as its draw opens.
class IdleHandshakeTests : public CliSimulationTestSuite {
public:
    /** Adds hunter 2 and puts all three PDNs on the stepped clock. */
    void SetUp() override {
        CliSimulationTestSuite::SetUp();
        // cli-main seeds this at boot; a hunter draws the match id from it.
        IdGenerator::initialize(42);
        devices.push_back(cli::DeviceFactory::createDevice(2, true));
        for (size_t index = 0; index < devices.size(); index++) {
            matchManagers[index] = devices[index].gameSession->getContext().matchManager;
        }
        SimpleTimer::setPlatformClock(&clock);
    }

    /** Hands the PDNs back to the real clock before they are destroyed. */
    void TearDown() override {
        SimpleTimer::setPlatformClock(&globalClock);
        CliSimulationTestSuite::TearDown();
    }

    /** The id of the state PDN @p index shows. */
    int stateOf(size_t index) { return devices[index].getCurrentState()->getStateId(); }

    /** One simulator tick. */
    void step() {
        clock.now += 5;
        cli::advanceSimulation(devices);
        for (size_t index = 0; index < devices.size(); index++) {
            if (stateOf(index) == MATCH && !matchManagers[index]->getHasPressedButton()) {
                devices[index].primaryButtonDriver->execCallback(ButtonInteraction::PRESS);
            }
        }
    }

    /** Ticks until @p done or @p maxTicks; @return whether @p done came true. */
    bool stepUntil(const std::function<bool()>& done, int maxTicks) {
        for (int tick = 0; tick < maxTicks; tick++) {
            if (done()) return true;
            step();
        }
        return done();
    }

    /** Cables hunter 0 to the bounty and runs their duel until both have stored it. */
    void playFirstDuel() {
        ASSERT_TRUE(cli::SerialCableBroker::getInstance().connect(0, 1));
        ASSERT_TRUE(stepUntil(
        [this]() {
            return matchManagers[0]->getMatchSequenceNumber() == 1 &&
                   matchManagers[1]->getMatchSequenceNumber() == 1;
        },
        20000));
        ASSERT_EQ(stateOf(2), IDLE);
        ASSERT_EQ(matchManagers[2]->getMatchSequenceNumber(), 0u);
    }

    /** Hunter 0 unplugs from the bounty and hunter 2 plugs in. */
    void swapHunters() {
        ASSERT_TRUE(cli::SerialCableBroker::getInstance().disconnect(0, 1));
        ASSERT_TRUE(cli::SerialCableBroker::getInstance().connect(2, 1));
    }

    SteppedClock clock;
    MatchManager* matchManagers[3] = {};
};

// Hunter 2 plugs into the bounty while it still shows the result of its duel
// with hunter 0. The bounty needs 8 s of result screen, 60 s of sleep and its
// wake-up before it is idle again; until then it cannot play, so hunter 2 must
// not be given a duel.
TEST_F(IdleHandshakeTests, hunterPluggedIntoABountyOnItsResultScreenIsNotGivenADuel) {
    ASSERT_NO_FATAL_FAILURE(playFirstDuel());
    ASSERT_TRUE(stateOf(1) == MATCH_RESULT || stateOf(1) == WIN || stateOf(1) == LOSE);
    ASSERT_NO_FATAL_FAILURE(swapHunters());

    // What the bounty showed when hunter 2's countdown began; -1 if it never began.
    int bountyStateAtCountdown = -1;
    ASSERT_TRUE(stepUntil(
    [this, &bountyStateAtCountdown]() {
        if (bountyStateAtCountdown < 0 && stateOf(2) == MATCH_COUNTDOWN) {
            bountyStateAtCountdown = stateOf(1);
        }
        return stateOf(1) == IDLE;
    },
    40000))
    << "the bounty never came back to its idle screen";

    EXPECT_EQ(bountyStateAtCountdown, -1)
    << "hunter 2 was counted down while the bounty showed "
    << cli::getStateName(bountyStateAtCountdown);
    EXPECT_EQ(devices[2].player->getLosses(), 0)
    << "hunter 2 was given a loss by a bounty that was not idle";
    EXPECT_EQ(matchManagers[2]->getMatchSequenceNumber(), 0u)
    << "hunter 2 stored a match the bounty never played";
    if (matchManagers[2]->getMatchSequenceNumber() == 1) {
        MatchRecord record{};
        ASSERT_TRUE(matchManagers[2]->readRecord(1, record));
        EXPECT_FALSE(record.winnerPlayerId == devices[1].player->getServerPlayerId())
        << "the stored match names the bounty, which never saw the duel, as its winner";
    }
    EXPECT_EQ(matchManagers[1]->getMatchSequenceNumber(), 1u);
}

// Hunter 2 plugs in a little later, while the bounty sleeps. The bounty ignores
// the handshake; once it is idle the hunter's retry gives both of them one duel.
TEST_F(IdleHandshakeTests, bountyAsleepIgnoresAHandshakeAndDuelsOnlyOnceIdle) {
    ASSERT_NO_FATAL_FAILURE(playFirstDuel());
    ASSERT_TRUE(stepUntil([this]() { return stateOf(1) == SLEEP; }, 4000));
    ASSERT_NO_FATAL_FAILURE(swapHunters());

    ASSERT_TRUE(stepUntil([this]() { return stateOf(1) == IDLE; }, 40000))
    << "the bounty never came back to its idle screen";
    EXPECT_EQ(devices[2].player->getLosses(), 0)
    << "hunter 2 was given a loss by a bounty that was asleep";
    EXPECT_EQ(matchManagers[2]->getMatchSequenceNumber(), 0u)
    << "hunter 2 stored a match the sleeping bounty never played";

    // Both players of one duel count down together. Three ticks cover the one
    // frame between the bounty's delivery report and the hunter reading the ack.
    if (stepUntil([this]() { return stateOf(1) == MATCH_COUNTDOWN; }, 2400)) {
        for (int tick = 0; tick < 3; tick++) step();
        EXPECT_EQ(stateOf(2), MATCH_COUNTDOWN)
        << "the bounty woke into a countdown while hunter 2 showed "
        << cli::getStateName(stateOf(2));
    }
}
