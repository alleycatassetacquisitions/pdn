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

TEST_F(SpikeVectorTestSuite, ShowDisplaysWaveInfo) {
    spikeVectorShowDisplaysWaveInfo(this);
}

TEST_F(SpikeVectorTestSuite, ShowTransitionsToGameplay) {
    spikeVectorShowTransitionsToGameplay(this);
}

TEST_F(SpikeVectorTestSuite, WallAdvancesWithTime) {
    spikeVectorWallAdvancesWithTime(this);
}

TEST_F(SpikeVectorTestSuite, CorrectDodgeAtGap) {
    spikeVectorCorrectDodgeAtGap(this);
}

TEST_F(SpikeVectorTestSuite, MissedDodge) {
    spikeVectorMissedDodge(this);
}

TEST_F(SpikeVectorTestSuite, WallTimeoutCausesEvaluate) {
    spikeVectorWallTimeoutCausesEvaluate(this);
}

TEST_F(SpikeVectorTestSuite, EvaluateRoutesToNextWave) {
    spikeVectorEvaluateRoutesToNextWave(this);
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

TEST_F(SpikeVectorTestSuite, DodgeAtBottomBoundary) {
    spikeVectorDodgeAtBottomBoundary(this);
}

TEST_F(SpikeVectorTestSuite, DodgeAtTopBoundary) {
    spikeVectorDodgeAtTopBoundary(this);
}

TEST_F(SpikeVectorTestSuite, ExactHitsEqualAllowed) {
    spikeVectorExactHitsEqualAllowed(this);
}

TEST_F(SpikeVectorTestSuite, WallArrivedFlagSet) {
    spikeVectorWallArrivedFlagSet(this);
}

TEST_F(SpikeVectorManagedTestSuite, ManagedModeReturns) {
    spikeVectorManagedModeReturns(this);
}

TEST_F(SpikeVectorTestSuite, RapidButtonInput) {
    spikeVectorRapidButtonInput(this);
}

TEST_F(SpikeVectorTestSuite, CursorBottomBoundaryClamp) {
    spikeVectorCursorBottomBoundaryClamp(this);
}

TEST_F(SpikeVectorTestSuite, CursorTopBoundaryClamp) {
    spikeVectorCursorTopBoundaryClamp(this);
}

TEST_F(SpikeVectorTestSuite, ScoreAccumulatesAcrossWaves) {
    spikeVectorScoreAccumulatesAcrossWaves(this);
}

TEST_F(SpikeVectorTestSuite, ButtonsIgnoredInNonGameplayStates) {
    spikeVectorButtonsIgnoredInNonGameplayStates(this);
}

TEST_F(SpikeVectorTestSuite, WallStartsAtZero) {
    spikeVectorWallStartsAtZero(this);
}

TEST_F(SpikeVectorTestSuite, GapPositionWithinRange) {
    spikeVectorGapPositionWithinRange(this);
}

TEST_F(SpikeVectorTestSuite, CursorResetsOnIntro) {
    spikeVectorCursorResetsOnIntro(this);
}

TEST_F(SpikeVectorTestSuite, WaveProgressionIncrement) {
    spikeVectorWaveProgressionIncrement(this);
}

TEST_F(SpikeVectorTestSuite, DeterministicRngGapPattern) {
    spikeVectorDeterministicRngGapPattern(this);
}

TEST_F(SpikeVectorTestSuite, LoseOnFinalWave) {
    spikeVectorLoseOnFinalWave(this);
}
