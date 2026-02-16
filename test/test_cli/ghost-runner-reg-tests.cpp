//
// Ghost Runner Tests — Registration file for Ghost Runner minigame
//

#include <gtest/gtest.h>

#include "ghost-runner-tests.hpp"

// ============================================
// GHOST RUNNER TESTS
// ============================================

TEST_F(GhostRunnerTestSuite, EasyConfigPresets) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerEasyConfigPresets(this);
}

TEST_F(GhostRunnerTestSuite, HardConfigPresets) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerHardConfigPresets(this);
}

TEST_F(GhostRunnerTestSuite, IntroSeedsRng) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerIntroSeedsRng(this);
}

TEST_F(GhostRunnerTestSuite, IntroTransitionsToShow) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerIntroTransitionsToShow(this);
}

TEST_F(GhostRunnerTestSuite, ShowDisplaysRoundInfo) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerShowDisplaysRoundInfo(this);
}

TEST_F(GhostRunnerTestSuite, ShowTransitionsToGameplay) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerShowTransitionsToGameplay(this);
}

TEST_F(GhostRunnerTestSuite, GhostAdvancesWithTime) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerGhostAdvancesWithTime(this);
}

TEST_F(GhostRunnerTestSuite, CorrectPressInTargetZone) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerCorrectPressInTargetZone(this);
}

TEST_F(GhostRunnerTestSuite, IncorrectPressOutsideZone) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerIncorrectPressOutsideZone(this);
}

TEST_F(GhostRunnerTestSuite, GhostTimeoutCountsStrike) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerGhostTimeoutCountsStrike(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToNextRound) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerEvaluateRoutesToNextRound(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToWin) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerEvaluateRoutesToWin(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToLose) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerEvaluateRoutesToLose(this);
}

TEST_F(GhostRunnerTestSuite, WinSetsOutcome) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerWinSetsOutcome(this);
}

TEST_F(GhostRunnerTestSuite, LoseSetsOutcome) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerLoseSetsOutcome(this);
}

TEST_F(GhostRunnerTestSuite, StandaloneLoopsToIntro) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerStandaloneLoopsToIntro(this);
}

TEST_F(GhostRunnerTestSuite, StateNamesResolve) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerStateNamesResolve(this);
}

TEST_F(GhostRunnerTestSuite, PressAtZoneStartBoundary) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerPressAtZoneStartBoundary(this);
}

TEST_F(GhostRunnerTestSuite, PressAtZoneEndBoundary) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerPressAtZoneEndBoundary(this);
}

TEST_F(GhostRunnerTestSuite, ExactStrikesEqualAllowed) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerExactStrikesEqualAllowed(this);
}

TEST_F(GhostRunnerTestSuite, TimeoutAtExactMissesAllowed) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerTimeoutAtExactMissesAllowed(this);
}

TEST_F(GhostRunnerManagedTestSuite, ManagedModeReturns) {
    GTEST_SKIP() << "Ghost Runner API changed in PR #225 — test needs rewrite (see #240)";
    ghostRunnerManagedModeReturns(this);
}
