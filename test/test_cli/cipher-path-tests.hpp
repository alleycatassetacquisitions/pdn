#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// CIPHER PATH TEST SUITE (standalone)
// ============================================

class CipherPathTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createGameDevice(0, "cipher-path");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = static_cast<CipherPath*>(device_.game);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            device_.clockDriver->advance(delayMs);
            device_.pdn->loop();
        }
    }

    DeviceInstance device_;
    CipherPath* game_ = nullptr;
};

// ============================================
// CIPHER PATH MANAGED TEST SUITE (FDN integration)
// ============================================

class CipherPathManagedTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            player_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            player_.pdn->loop();
        }
    }

    void advanceToIdle() {
        player_.game->skipToState(player_.pdn, 7);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentState()->getStateId();
    }

    CipherPath* getCipherPath() {
        return static_cast<CipherPath*>(
            player_.pdn->getApp(StateId(CIPHER_PATH_APP_ID)));
    }

    DeviceInstance player_;
};

// ============================================
// CONFIG PRESET TESTS
// ============================================

/*
 * Test: EASY config has gridSize=6, moveBudget=12, rounds=2.
 */
void cipherPathEasyConfigPresets(CipherPathTestSuite* suite) {
    ASSERT_EQ(CIPHER_PATH_EASY.gridSize, 6);
    ASSERT_EQ(CIPHER_PATH_EASY.moveBudget, 12);
    ASSERT_EQ(CIPHER_PATH_EASY.rounds, 2);
}

/*
 * Test: HARD config has gridSize=10, moveBudget=14, rounds=4.
 */
void cipherPathHardConfigPresets(CipherPathTestSuite* suite) {
    ASSERT_EQ(CIPHER_PATH_HARD.gridSize, 10);
    ASSERT_EQ(CIPHER_PATH_HARD.moveBudget, 14);
    ASSERT_EQ(CIPHER_PATH_HARD.rounds, 4);
}

// ============================================
// INTRO TESTS
// ============================================

/*
 * Test: Intro resets session fields to defaults.
 * Dirty the session, then skip to Intro and verify reset.
 */
void cipherPathIntroResetsSession(CipherPathTestSuite* suite) {
    // Dirty the session
    auto& session = suite->game_->getSession();
    session.playerPosition = 5;
    session.movesUsed = 10;
    session.currentRound = 3;
    session.score = 500;

    // Skip to Intro (index 0)
    suite->game_->skipToState(suite->device_.pdn, 0);
    suite->tick(1);

    // Verify session was reset
    ASSERT_EQ(session.playerPosition, 0);
    ASSERT_EQ(session.movesUsed, 0);
    ASSERT_EQ(session.currentRound, 0);
    ASSERT_EQ(session.score, 0);
}

/*
 * Test: Intro transitions to Show after timer expires.
 */
void cipherPathIntroTransitionsToShow(CipherPathTestSuite* suite) {
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_INTRO);

    // Advance past 2s intro timer (use generous buffer)
    suite->tickWithTime(30, 100);

    // After intro timer, should be in Show (or Gameplay if Show timer also expired)
    int stateId = suite->game_->getCurrentState()->getStateId();
    ASSERT_TRUE(stateId == CIPHER_SHOW || stateId == CIPHER_GAMEPLAY)
        << "Expected Show or Gameplay after intro, got " << stateId;

    // More robust: skip to Intro, advance with enough time for intro but not show
    suite->game_->skipToState(suite->device_.pdn, 0);
    suite->tick(1);
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_INTRO);

    // Advance in small increments, checking for transition
    for (int i = 0; i < 30; i++) {
        suite->device_.clockDriver->advance(100);
        suite->device_.pdn->loop();
        if (suite->game_->getCurrentState()->getStateId() != CIPHER_INTRO) {
            break;
        }
    }

    // First transition from Intro should be to Show
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_SHOW);
}

// ============================================
// SHOW TESTS
// ============================================

/*
 * Test: Show state has correct state ID when entered.
 */
void cipherPathShowDisplaysRoundInfo(CipherPathTestSuite* suite) {
    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_SHOW);
}

/*
 * Test: Show generates cipher array with values.
 */
void cipherPathShowGeneratesCipher(CipherPathTestSuite* suite) {
    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->tick(1);

    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // At least one cipher value should be 0 or 1 (valid values)
    bool hasCipherValues = false;
    for (int i = 0; i < config.gridSize; i++) {
        if (session.cipher[i] == 0 || session.cipher[i] == 1) {
            hasCipherValues = true;
            break;
        }
    }
    ASSERT_TRUE(hasCipherValues);
}

/*
 * Test: Show transitions to Gameplay after timer expires.
 */
void cipherPathShowTransitionsToGameplay(CipherPathTestSuite* suite) {
    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->tick(1);
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_SHOW);

    // Advance past 1.5s show timer
    suite->tickWithTime(20, 100);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_GAMEPLAY);
}

// ============================================
// GAMEPLAY TESTS
// ============================================

/*
 * Test: Pressing the correct button advances player position.
 */
void cipherPathCorrectMoveAdvancesPosition(CipherPathTestSuite* suite) {
    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    auto& session = suite->game_->getSession();
    int correctDir = session.cipher[session.playerPosition]; // 0=UP, 1=DOWN

    // Press correct button
    if (correctDir == 0) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    } else {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    suite->tick(2);

    ASSERT_EQ(session.playerPosition, 1);
    ASSERT_EQ(session.movesUsed, 1);
    ASSERT_TRUE(session.lastMoveCorrect);
}

/*
 * Test: Pressing the wrong button wastes a move without advancing position.
 */
void cipherPathWrongMoveWastesMove(CipherPathTestSuite* suite) {
    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    auto& session = suite->game_->getSession();
    int correctDir = session.cipher[session.playerPosition]; // 0=UP, 1=DOWN

    // Press wrong button (opposite of correct)
    if (correctDir == 0) {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    } else {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    suite->tick(2);

    ASSERT_EQ(session.playerPosition, 0);  // Position unchanged
    ASSERT_EQ(session.movesUsed, 1);       // Move consumed
    ASSERT_FALSE(session.lastMoveCorrect);
}

/*
 * Test: Reaching the exit triggers transition through Evaluate.
 * Evaluate is a pass-through state that immediately routes to Show (next round)
 * or Win (last round), so we verify the post-Evaluate destination.
 */
void cipherPathReachExitTriggersEvaluate(CipherPathTestSuite* suite) {
    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set position near exit, round 0 (more rounds remain)
    session.playerPosition = config.gridSize - 2;
    session.currentRound = 0;

    // Press correct button for this position
    int correctDir = session.cipher[session.playerPosition];
    if (correctDir == 0) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    } else {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    suite->tick(2);

    // Evaluate routes through to Show (next round) since more rounds remain
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_SHOW);
}

/*
 * Test: Exhausting the move budget triggers transition through Evaluate to Lose.
 * Evaluate is a pass-through state that routes to Lose when budget is exhausted
 * without reaching the exit.
 */
void cipherPathBudgetExhaustedTriggersEvaluate(CipherPathTestSuite* suite) {
    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set movesUsed near budget (one move left), position NOT at exit
    session.movesUsed = config.moveBudget - 1;
    session.playerPosition = 0;

    // Press wrong button to exhaust budget without advancing
    int correctDir = session.cipher[session.playerPosition];
    if (correctDir == 0) {
        // Press wrong (DOWN)
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    } else {
        // Press wrong (UP)
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    suite->tick(2);

    // Evaluate routes through to Lose since budget exhausted without reaching exit
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_LOSE);
}

// ============================================
// EVALUATE TESTS
// ============================================

/*
 * Test: When player reached exit and more rounds remain, routes to Show.
 */
void cipherPathEvaluateRoutesToNextRound(CipherPathTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Setup: player reached exit, round 0 of 2 (easy mode)
    session.playerPosition = config.gridSize - 1;
    session.currentRound = 0;

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->tick(2);

    // Should route to Show for next round
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_SHOW);
    ASSERT_EQ(session.currentRound, 1);
}

/*
 * Test: When player reached exit on last round, routes to Win.
 */
void cipherPathEvaluateRoutesToWin(CipherPathTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Setup: player reached exit on last round
    session.playerPosition = config.gridSize - 1;
    session.currentRound = config.rounds - 1;

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->tick(2);

    // Should route to Win
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_WIN);
}

/*
 * Test: When budget exhausted without reaching exit, routes to Lose.
 */
void cipherPathEvaluateRoutesToLose(CipherPathTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Setup: budget exhausted, player not at exit
    session.playerPosition = 2;  // Not at exit
    session.movesUsed = config.moveBudget;

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->tick(2);

    // Should route to Lose
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_LOSE);
}

// ============================================
// WIN/LOSE OUTCOME TESTS
// ============================================

/*
 * Test: Win state sets outcome to WON with the session score.
 */
void cipherPathWinSetsOutcome(CipherPathTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.score = 200;

    // Skip to Win (index 4)
    suite->game_->skipToState(suite->device_.pdn, 4);
    suite->tick(1);

    ASSERT_EQ(suite->game_->getOutcome().result, MiniGameResult::WON);
    ASSERT_EQ(suite->game_->getOutcome().score, 200);
}

/*
 * Test: Lose state sets outcome to LOST with session score.
 */
void cipherPathLoseSetsOutcome(CipherPathTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.score = 100;

    // Skip to Lose (index 5)
    suite->game_->skipToState(suite->device_.pdn, 5);
    suite->tick(1);

    ASSERT_EQ(suite->game_->getOutcome().result, MiniGameResult::LOST);
    ASSERT_EQ(suite->game_->getOutcome().score, 100);
}

// ============================================
// STANDALONE LOOP TEST
// ============================================

/*
 * Test: In standalone mode, Win loops back to Intro after timer.
 */
void cipherPathStandaloneLoopsToIntro(CipherPathTestSuite* suite) {
    // Skip to Win (index 4)
    suite->game_->skipToState(suite->device_.pdn, 4);
    suite->tick(1);
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_WIN);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), CIPHER_INTRO);
}

// ============================================
// STATE NAME TESTS
// ============================================

/*
 * Test: All 6 state names resolve correctly.
 */
void cipherPathStateNamesResolve(CipherPathTestSuite* suite) {
    ASSERT_STREQ(getCipherPathStateName(CIPHER_INTRO), "CipherPathIntro");
    ASSERT_STREQ(getCipherPathStateName(CIPHER_WIN), "CipherPathWin");
    ASSERT_STREQ(getCipherPathStateName(CIPHER_LOSE), "CipherPathLose");
    ASSERT_STREQ(getCipherPathStateName(CIPHER_SHOW), "CipherPathShow");
    ASSERT_STREQ(getCipherPathStateName(CIPHER_GAMEPLAY), "CipherPathGameplay");
    ASSERT_STREQ(getCipherPathStateName(CIPHER_EVALUATE), "CipherPathEvaluate");

    // Verify getStateName routes correctly for cipher path range
    ASSERT_STREQ(getStateName(CIPHER_INTRO), "CipherPathIntro");
    ASSERT_STREQ(getStateName(CIPHER_GAMEPLAY), "CipherPathGameplay");
}

// ============================================
// MANAGED MODE TEST
// ============================================

/*
 * Test: Full FDN flow — launches Cipher Path in managed mode,
 * plays through all rounds to win, returns to FdnComplete.
 */
void cipherPathManagedModeReturns(CipherPathManagedTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN handshake for Cipher Path (GameType 4, KonamiButton RIGHT=3)
    suite->player_.serialOutDriver->injectInput("*fdn:4:3\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Should be in Cipher Path Intro now
    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);
    ASSERT_TRUE(cp->getConfig().managedMode);

    auto& config = cp->getConfig();

    // Play through all rounds
    for (int round = 0; round < config.rounds; round++) {
        // Wait for Intro to transition (first round only)
        if (round == 0) {
            // Advance past intro timer (2s)
            suite->tickWithTime(25, 100);
        }

        // Should be in Show state now
        ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_SHOW);

        // Advance past show timer (1.5s)
        suite->tickWithTime(20, 100);

        // Should be in Gameplay state
        ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_GAMEPLAY);

        auto& sess = cp->getSession();

        // Make correct moves until exit is reached.
        // After each correct move, check if we're still in Gameplay.
        // When we reach the exit, the state transitions through Evaluate.
        for (int step = 0; step < config.gridSize; step++) {
            if (cp->getCurrentState()->getStateId() != CIPHER_GAMEPLAY) {
                break;
            }
            int correctDir = sess.cipher[sess.playerPosition];
            if (correctDir == 0) {
                suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            } else {
                suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            }
            suite->tick(2);
        }

        // After reaching exit, Evaluate routes to Show (next round) or Win (last round)
    }

    // After last round, should be in Win
    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_WIN);
    ASSERT_EQ(cp->getOutcome().result, MiniGameResult::WON);

    // Advance past win timer (3s) — managed mode returns to previous app
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

#endif // NATIVE_BUILD
