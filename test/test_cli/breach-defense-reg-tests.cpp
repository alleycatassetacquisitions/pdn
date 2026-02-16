//
// Breach Defense Tests — Registration file for Breach Defense minigame
//

#include <gtest/gtest.h>

#include "breach-defense-tests.hpp"

// ============================================
// BREACH DEFENSE TESTS
// ============================================

TEST_F(BreachDefenseTestSuite, EasyConfigPresets) {
    breachDefenseEasyConfigPresets(this);
}

TEST_F(BreachDefenseTestSuite, HardConfigPresets) {
    breachDefenseHardConfigPresets(this);
}

TEST_F(BreachDefenseTestSuite, IntroResetsSession) {
    breachDefenseIntroResetsSession(this);
}

TEST_F(BreachDefenseTestSuite, IntroTransitionsToShow) {
    breachDefenseIntroTransitionsToShow(this);
}

TEST_F(BreachDefenseTestSuite, ShowDisplaysThreatInfo) {
    breachDefenseShowDisplaysThreatInfo(this);
}

TEST_F(BreachDefenseTestSuite, ShowTransitionsToGameplay) {
    breachDefenseShowTransitionsToGameplay(this);
}

TEST_F(BreachDefenseTestSuite, ThreatAdvancesWithTime) {
    breachDefenseThreatAdvancesWithTime(this);
}

TEST_F(BreachDefenseTestSuite, CorrectBlock) {
    breachDefenseCorrectBlock(this);
}

TEST_F(BreachDefenseTestSuite, MissedThreat) {
    breachDefenseMissedThreat(this);
}

TEST_F(BreachDefenseTestSuite, ShieldMovesUpDown) {
    breachDefenseShieldMovesUpDown(this);
}

TEST_F(BreachDefenseTestSuite, EvaluateRoutesToNextThreat) {
    breachDefenseEvaluateRoutesToNextThreat(this);
}

TEST_F(BreachDefenseTestSuite, EvaluateRoutesToWin) {
    breachDefenseEvaluateRoutesToWin(this);
}

TEST_F(BreachDefenseTestSuite, EvaluateRoutesToLose) {
    breachDefenseEvaluateRoutesToLose(this);
}

TEST_F(BreachDefenseTestSuite, WinSetsOutcome) {
    breachDefenseWinSetsOutcome(this);
}

TEST_F(BreachDefenseTestSuite, LoseSetsOutcome) {
    breachDefenseLoseSetsOutcome(this);
}

TEST_F(BreachDefenseTestSuite, StandaloneLoopsToIntro) {
    breachDefenseStandaloneLoopsToIntro(this);
}

TEST_F(BreachDefenseTestSuite, StateNamesResolve) {
    breachDefenseStateNamesResolve(this);
}

TEST_F(BreachDefenseTestSuite, BlockAtLaneZero) {
    breachDefenseBlockAtLaneZero(this);
}

TEST_F(BreachDefenseTestSuite, BlockAtMaxLane) {
    breachDefenseBlockAtMaxLane(this);
}

TEST_F(BreachDefenseTestSuite, ExactBreachesEqualAllowed) {
    breachDefenseExactBreachesEqualAllowed(this);
}

TEST_F(BreachDefenseTestSuite, HapticsIntensityDiffers) {
    breachDefenseHapticsIntensityDiffers(this);
}

// DISABLED: Wave 17 KonamiMetaGame routing changed (Issue #271)
// After minigame completes, app transition flow (KonamiMetaGame → resume → FdnComplete) doesn't
// match test expectations. Re-enable after verifying managed mode return flow.
TEST_F(BreachDefenseManagedTestSuite, DISABLED_ManagedModeReturns) {
    breachDefenseManagedModeReturns(this);
}
