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
