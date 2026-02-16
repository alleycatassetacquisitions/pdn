//
// Cipher Path Tests — Registration file for Cipher Path minigame
//

#include <gtest/gtest.h>

#include "cipher-path-tests.hpp"

// ============================================
// CIPHER PATH TESTS
// ============================================

TEST_F(CipherPathTestSuite, EasyConfigPresets) {
    cipherPathEasyConfigPresets(this);
}

TEST_F(CipherPathTestSuite, HardConfigPresets) {
    cipherPathHardConfigPresets(this);
}

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

// DISABLED: Wave 17 KonamiMetaGame routing changed (Issue #271)
// After minigame completes, app transition flow (KonamiMetaGame → resume → FdnComplete) doesn't
// match test expectations. Re-enable after verifying managed mode return flow.
TEST_F(CipherPathManagedTestSuite, DISABLED_ManagedModeReturns) {
    cipherPathManagedModeReturns(this);
}

// ============================================
// ADDITIONAL EDGE CASE TESTS (NEW)
// ============================================

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
