//
// Ghost Runner Tests — Registration file for Ghost Runner minigame
//

#include <gtest/gtest.h>

#include "ghost-runner-tests.hpp"

// ============================================
// GHOST RUNNER TESTS
// ============================================

TEST_F(GhostRunnerTestSuite, EasyConfigPresets) {
    ghostRunnerEasyConfigPresets(this);
}

TEST_F(GhostRunnerTestSuite, HardConfigPresets) {
    ghostRunnerHardConfigPresets(this);
}

TEST_F(GhostRunnerTestSuite, IntroSeedsRng) {
    ghostRunnerIntroSeedsRng(this);
}

TEST_F(GhostRunnerTestSuite, IntroTransitionsToShow) {
    ghostRunnerIntroTransitionsToShow(this);
}

TEST_F(GhostRunnerTestSuite, ShowDisplaysRoundInfo) {
    ghostRunnerShowDisplaysRoundInfo(this);
}

TEST_F(GhostRunnerTestSuite, ShowTransitionsToGameplay) {
    ghostRunnerShowTransitionsToGameplay(this);
}

TEST_F(GhostRunnerTestSuite, GhostAdvancesWithTime) {
    ghostRunnerGhostAdvancesWithTime(this);
}

TEST_F(GhostRunnerTestSuite, CorrectPressInTargetZone) {
    ghostRunnerCorrectPressInTargetZone(this);
}

TEST_F(GhostRunnerTestSuite, IncorrectPressOutsideZone) {
    ghostRunnerIncorrectPressOutsideZone(this);
}

TEST_F(GhostRunnerTestSuite, GhostTimeoutCountsStrike) {
    ghostRunnerGhostTimeoutCountsStrike(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToNextRound) {
    ghostRunnerEvaluateRoutesToNextRound(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToWin) {
    ghostRunnerEvaluateRoutesToWin(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToLose) {
    ghostRunnerEvaluateRoutesToLose(this);
}

TEST_F(GhostRunnerTestSuite, WinSetsOutcome) {
    ghostRunnerWinSetsOutcome(this);
}

TEST_F(GhostRunnerTestSuite, LoseSetsOutcome) {
    ghostRunnerLoseSetsOutcome(this);
}

TEST_F(GhostRunnerTestSuite, StandaloneLoopsToIntro) {
    ghostRunnerStandaloneLoopsToIntro(this);
}

TEST_F(GhostRunnerTestSuite, StateNamesResolve) {
    ghostRunnerStateNamesResolve(this);
}

TEST_F(GhostRunnerTestSuite, PressAtZoneStartBoundary) {
    ghostRunnerPressAtZoneStartBoundary(this);
}

TEST_F(GhostRunnerTestSuite, PressAtZoneEndBoundary) {
    ghostRunnerPressAtZoneEndBoundary(this);
}

TEST_F(GhostRunnerTestSuite, ExactStrikesEqualAllowed) {
    ghostRunnerExactStrikesEqualAllowed(this);
}

TEST_F(GhostRunnerTestSuite, TimeoutAtExactMissesAllowed) {
    ghostRunnerTimeoutAtExactMissesAllowed(this);
}

TEST_F(GhostRunnerManagedTestSuite, ManagedModeReturns) {
    ghostRunnerManagedModeReturns(this);
}
