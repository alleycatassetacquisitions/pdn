//
// Ghost Runner Tests — Registration file for Ghost Runner minigame
//
// NOTE: Tests rewritten for Wave 18 Memory Maze redesign (#327)
// Ghost Runner was redesigned from a Guitar Hero rhythm game to a Memory Maze.
// The old API (currentPattern, Lane, NoteGrade, ghostSpeedMs, notesPerRound)
// was completely replaced with a maze-based API (cursorRow/Col, walls[], solutionPath[]).
//

#include <gtest/gtest.h>

#include "ghost-runner-tests.hpp"

// ============================================
// GHOST RUNNER CONFIG TESTS
// ============================================

TEST_F(GhostRunnerTestSuite, EasyConfigPresets) {
    ghostRunnerEasyConfigPresets(this);
}

TEST_F(GhostRunnerTestSuite, HardConfigPresets) {
    ghostRunnerHardConfigPresets(this);
}

TEST_F(GhostRunnerTestSuite, SessionResetClearsState) {
    ghostRunnerSessionResetClearsState(this);
}

// ============================================
// GHOST RUNNER STATE TRANSITION TESTS
// ============================================

TEST_F(GhostRunnerTestSuite, IntroResetsSession) {
    ghostRunnerIntroResetsSession(this);
}

TEST_F(GhostRunnerTestSuite, IntroTransitionsToShow) {
    ghostRunnerIntroTransitionsToShow(this);
}

TEST_F(GhostRunnerTestSuite, ShowDisplaysMaze) {
    ghostRunnerShowDisplaysMaze(this);
}

TEST_F(GhostRunnerTestSuite, ShowTransitionsToGameplay) {
    ghostRunnerShowTransitionsToGameplay(this);
}

// ============================================
// GHOST RUNNER GAMEPLAY TESTS (Maze Navigation)
// ============================================

TEST_F(GhostRunnerTestSuite, DirectionCycling) {
    ghostRunnerDirectionCycling(this);
}

TEST_F(GhostRunnerTestSuite, ValidMove) {
    ghostRunnerValidMove(this);
}

TEST_F(GhostRunnerTestSuite, BonkIntoWall) {
    ghostRunnerBonkIntoWall(this);
}

TEST_F(GhostRunnerTestSuite, BonkOutOfBounds) {
    ghostRunnerBonkOutOfBounds(this);
}

TEST_F(GhostRunnerTestSuite, DISABLED_ReachingExit) {
    ghostRunnerReachingExit(this);
}

// ============================================
// GHOST RUNNER EVALUATE TESTS
// ============================================

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToNextRound) {
    ghostRunnerEvaluateRoutesToNextRound(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToWin) {
    ghostRunnerEvaluateRoutesToWin(this);
}

TEST_F(GhostRunnerTestSuite, EvaluateRoutesToLose) {
    ghostRunnerEvaluateRoutesToLose(this);
}

// ============================================
// GHOST RUNNER OUTCOME TESTS
// ============================================

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

// ============================================
// GHOST RUNNER EDGE CASES
// ============================================

TEST_F(GhostRunnerTestSuite, ExactLifeRemainingContinues) {
    ghostRunnerExactLifeRemainingContinues(this);
}

TEST_F(GhostRunnerTestSuite, BonkAtLastLife) {
    ghostRunnerBonkAtLastLife(this);
}

// ============================================
// GHOST RUNNER MANAGED MODE (FDN Integration)
// ============================================

TEST_F(GhostRunnerManagedTestSuite, ManagedModeReturns) {
    ghostRunnerManagedModeReturns(this);
}
