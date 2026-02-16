//
// Cipher Path Tests — Registration file for Cipher Path minigame
//
// NOTE: Tests temporarily disabled during Wave 18 redesign (#242)
// Cipher Path was redesigned from a binary cipher path to a wire routing puzzle.
// The old API (cipher[], playerPosition, movesUsed, moveBudget, gridSize)
// was completely replaced with tile-based grid API (tileType[], tileRotation[],
// cursorPathIndex, flowTileIndex, flowActive).
// Tests need complete rewrite for the new wire rotation mechanics.
//
// TODO(#242): Rewrite tests for wire routing puzzle mechanics

#include <gtest/gtest.h>

// Temporarily not including header since all gameplay tests disabled
// #include "cipher-path-tests.hpp"

// Minimal includes for config tests only
#include "game/cipher-path/cipher-path.hpp"

// ============================================
// CIPHER PATH TESTS — TEMPORARILY DISABLED
// ============================================

// Minimal test suite for basic sanity checks
class CipherPathTestSuite : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

// Config tests updated for new Wire Routing API
TEST_F(CipherPathTestSuite, EasyConfigPresets) {
    CipherPathConfig easy = CIPHER_PATH_EASY;
    ASSERT_EQ(easy.cols, 5);
    ASSERT_EQ(easy.rows, 4);
    ASSERT_EQ(easy.rounds, 1);
    ASSERT_EQ(easy.flowSpeedMs, 200);
    ASSERT_EQ(easy.flowSpeedDecayMs, 0);
    ASSERT_EQ(easy.noisePercent, 30);
}

TEST_F(CipherPathTestSuite, HardConfigPresets) {
    CipherPathConfig hard = CIPHER_PATH_HARD;
    ASSERT_EQ(hard.cols, 7);
    ASSERT_EQ(hard.rows, 5);
    ASSERT_EQ(hard.rounds, 3);
    ASSERT_EQ(hard.flowSpeedMs, 80);
    ASSERT_EQ(hard.flowSpeedDecayMs, 10);
    ASSERT_EQ(hard.noisePercent, 40);
}

TEST_F(CipherPathTestSuite, SessionResetClearsState) {
    CipherPathSession session;
    session.currentRound = 2;
    session.score = 50;
    session.pathLength = 10;
    session.flowTileIndex = 5;
    session.flowPixelInTile = 3;
    session.flowActive = true;
    session.cursorPathIndex = 7;

    session.reset();

    ASSERT_EQ(session.currentRound, 0);
    ASSERT_EQ(session.score, 0);
    ASSERT_EQ(session.pathLength, 0);
    ASSERT_EQ(session.flowTileIndex, 0);
    ASSERT_EQ(session.flowPixelInTile, 0);
    ASSERT_FALSE(session.flowActive);
    ASSERT_EQ(session.cursorPathIndex, 0);
    // Verify tile arrays are cleared
    for (int i = 0; i < 35; i++) {
        ASSERT_EQ(session.tileType[i], 0);
        ASSERT_EQ(session.tileRotation[i], 0);
        ASSERT_EQ(session.correctRotation[i], 0);
        ASSERT_EQ(session.pathOrder[i], -1);
    }
}

// All gameplay tests disabled pending rewrite for wire routing mechanics
/*
TEST_F(CipherPathTestSuite, IntroResetsSession) {
    cipherPathIntroResetsSession(this);
}

TEST_F(CipherPathTestSuite, IntroTransitionsToShow) {
    cipherPathIntroTransitionsToShow(this);
}

TEST_F(CipherPathTestSuite, ShowDisplaysRoundInfo) {
    cipherPathShowDisplaysRoundInfo(this);
}

TEST_F(CipherPathTestSuite, ShowGeneratesCipher) {
    cipherPathShowGeneratesCipher(this);
}

TEST_F(CipherPathTestSuite, ShowTransitionsToGameplay) {
    cipherPathShowTransitionsToGameplay(this);
}

TEST_F(CipherPathTestSuite, CorrectMoveAdvancesPosition) {
    cipherPathCorrectMoveAdvancesPosition(this);
}

TEST_F(CipherPathTestSuite, WrongMoveWastesMove) {
    cipherPathWrongMoveWastesMove(this);
}

TEST_F(CipherPathTestSuite, ReachExitTriggersEvaluate) {
    cipherPathReachExitTriggersEvaluate(this);
}

TEST_F(CipherPathTestSuite, BudgetExhaustedTriggersEvaluate) {
    cipherPathBudgetExhaustedTriggersEvaluate(this);
}

TEST_F(CipherPathTestSuite, EvaluateRoutesToNextRound) {
    cipherPathEvaluateRoutesToNextRound(this);
}

TEST_F(CipherPathTestSuite, EvaluateRoutesToWin) {
    cipherPathEvaluateRoutesToWin(this);
}

TEST_F(CipherPathTestSuite, EvaluateRoutesToLose) {
    cipherPathEvaluateRoutesToLose(this);
}

TEST_F(CipherPathTestSuite, WinSetsOutcome) {
    cipherPathWinSetsOutcome(this);
}

TEST_F(CipherPathTestSuite, LoseSetsOutcome) {
    cipherPathLoseSetsOutcome(this);
}

TEST_F(CipherPathTestSuite, StandaloneLoopsToIntro) {
    cipherPathStandaloneLoopsToIntro(this);
}

TEST_F(CipherPathTestSuite, StateNamesResolve) {
    cipherPathStateNamesResolve(this);
}

TEST_F(CipherPathTestSuite, BudgetExhaustedAtStart) {
    cipherPathBudgetExhaustedAtStart(this);
}

TEST_F(CipherPathTestSuite, ReachExitExactPosition) {
    cipherPathReachExitExactPosition(this);
}

TEST_F(CipherPathTestSuite, BudgetExhaustedNearExit) {
    cipherPathBudgetExhaustedNearExit(this);
}

TEST_F(CipherPathTestSuite, MoveFromStartBoundary) {
    cipherPathMoveFromStartBoundary(this);
}

TEST_F(CipherPathManagedTestSuite, DISABLED_ManagedModeReturns) {
    cipherPathManagedModeReturns(this);
}

TEST_F(CipherPathTestSuite, MultipleConsecutiveWrongMoves) {
    cipherPathMultipleConsecutiveWrongMoves(this);
}

TEST_F(CipherPathTestSuite, BudgetEqualsGridSize) {
    cipherPathBudgetEqualsGridSize(this);
}

TEST_F(CipherPathTestSuite, ReachExitMidGame) {
    cipherPathReachExitMidGame(this);
}

TEST_F(CipherPathTestSuite, HardModePerfectPlayRequired) {
    cipherPathHardModePerfectPlayRequired(this);
}

TEST_F(CipherPathTestSuite, ExitReachedAtBudgetLimit) {
    cipherPathExitReachedAtBudgetLimit(this);
}

TEST_F(CipherPathTestSuite, WrongMoveAtBudgetLimit) {
    cipherPathWrongMoveAtBudgetLimit(this);
}
*/
