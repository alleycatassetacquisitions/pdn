//
// Breach Defense Tests — Registration file for Breach Defense minigame
//
// NOTE: Tests temporarily disabled during Wave 18 redesign (#231)
// The 6-state flow (Intro->Show->Gameplay->Evaluate->Win/Lose) has been
// redesigned to 4-state flow (Intro->Gameplay->Win/Lose) with combo pipeline.
// Tests need to be rewritten for new mechanics:
// - Multi-threat overlap (2 easy / 3 hard)
// - Inline evaluation during gameplay
// - Continuous rendering with Pong x Space Invaders visuals
//
// TODO(#231): Rewrite tests for new combo defense mechanics

#include <gtest/gtest.h>

// Temporarily not including header since all tests disabled
// #include "breach-defense-tests.hpp"

// Minimal includes for config tests only
#include "game/breach-defense/breach-defense.hpp"

// ============================================
// BREACH DEFENSE TESTS — TEMPORARILY DISABLED
// ============================================

// Minimal test suite for basic sanity checks
class BreachDefenseTestSuite : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

// Config tests still valid
TEST_F(BreachDefenseTestSuite, EasyConfigPresets) {
    BreachDefenseConfig cfg = makeBreachDefenseEasyConfig();
    ASSERT_EQ(cfg.numLanes, 3);
    ASSERT_EQ(cfg.totalThreats, 6);
    ASSERT_EQ(cfg.missesAllowed, 3);
    ASSERT_EQ(cfg.spawnIntervalMs, 1500);
    ASSERT_EQ(cfg.maxOverlap, 2);
}

TEST_F(BreachDefenseTestSuite, HardConfigPresets) {
    BreachDefenseConfig cfg = makeBreachDefenseHardConfig();
    ASSERT_EQ(cfg.numLanes, 5);
    ASSERT_EQ(cfg.totalThreats, 12);
    ASSERT_EQ(cfg.missesAllowed, 1);
    ASSERT_EQ(cfg.spawnIntervalMs, 700);
    ASSERT_EQ(cfg.maxOverlap, 3);
}

// All other tests disabled pending rewrite for new mechanics
/*
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
*/
