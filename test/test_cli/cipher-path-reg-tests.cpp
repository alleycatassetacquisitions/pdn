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

TEST_F(CipherPathManagedTestSuite, ManagedModeReturns) {
    cipherPathManagedModeReturns(this);
}
