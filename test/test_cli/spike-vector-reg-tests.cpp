//
// Spike Vector Tests — Registration file for Spike Vector minigame
//

#include <gtest/gtest.h>

#include "spike-vector-tests.hpp"

// ============================================
// SPIKE VECTOR TESTS
// ============================================

TEST_F(SpikeVectorTestSuite, EasyConfigPresets) {
    spikeVectorEasyConfigPresets(this);
}

TEST_F(SpikeVectorTestSuite, HardConfigPresets) {
    spikeVectorHardConfigPresets(this);
}

TEST_F(SpikeVectorTestSuite, IntroResetsSession) {
    spikeVectorIntroResetsSession(this);
}

TEST_F(SpikeVectorTestSuite, IntroTransitionsToShow) {
    spikeVectorIntroTransitionsToShow(this);
}

TEST_F(SpikeVectorTestSuite, ShowGeneratesGaps) {
    spikeVectorShowGeneratesGaps(this);
}

TEST_F(SpikeVectorTestSuite, ShowTransitionsToGameplay) {
    spikeVectorShowTransitionsToGameplay(this);
}

TEST_F(SpikeVectorTestSuite, FormationAdvances) {
    spikeVectorFormationAdvances(this);
}

TEST_F(SpikeVectorTestSuite, DISABLED_CorrectDodge) {
    spikeVectorCorrectDodge(this);
}

TEST_F(SpikeVectorTestSuite, DISABLED_MissedDodge) {
    spikeVectorMissedDodge(this);
}

TEST_F(SpikeVectorTestSuite, DISABLED_FormationCompleteTransition) {
    spikeVectorFormationCompleteTransition(this);
}

TEST_F(SpikeVectorTestSuite, EvaluateRoutesToNextLevel) {
    spikeVectorEvaluateRoutesToNextLevel(this);
}

TEST_F(SpikeVectorTestSuite, EvaluateRoutesToWin) {
    spikeVectorEvaluateRoutesToWin(this);
}

TEST_F(SpikeVectorTestSuite, EvaluateRoutesToLose) {
    spikeVectorEvaluateRoutesToLose(this);
}

TEST_F(SpikeVectorTestSuite, WinSetsOutcome) {
    spikeVectorWinSetsOutcome(this);
}

TEST_F(SpikeVectorTestSuite, LoseSetsOutcome) {
    spikeVectorLoseSetsOutcome(this);
}

TEST_F(SpikeVectorTestSuite, StandaloneLoopsToIntro) {
    spikeVectorStandaloneLoopsToIntro(this);
}

TEST_F(SpikeVectorTestSuite, StateNamesResolve) {
    spikeVectorStateNamesResolve(this);
}

TEST_F(SpikeVectorManagedTestSuite, DISABLED_ManagedModeReturns) {
    spikeVectorManagedModeReturns(this);
}

TEST_F(SpikeVectorTestSuite, CursorBottomBoundaryClamp) {
    spikeVectorCursorBottomBoundaryClamp(this);
}

TEST_F(SpikeVectorTestSuite, CursorTopBoundaryClamp) {
    spikeVectorCursorTopBoundaryClamp(this);
}

TEST_F(SpikeVectorTestSuite, LevelProgressionIncrement) {
    spikeVectorLevelProgressionIncrement(this);
}

TEST_F(SpikeVectorTestSuite, DeterministicRngGapPattern) {
    spikeVectorDeterministicRngGapPattern(this);
}

TEST_F(SpikeVectorTestSuite, LoseOnFinalLevel) {
    spikeVectorLoseOnFinalLevel(this);
}
