//
// Ghost Runner Tests — Registration file for Ghost Runner minigame
//
// NOTE: Tests temporarily disabled during Wave 18 redesign (#220)
// Ghost Runner was redesigned from a Guitar Hero rhythm game to a Memory Maze.
// The old API (currentPattern, Lane, NoteGrade, ghostSpeedMs, notesPerRound)
// was completely replaced with a maze-based API (cursorRow/Col, walls[], solutionPath[]).
// Tests need complete rewrite for the new maze navigation mechanics.
//
// TODO(#220): Rewrite tests for memory maze mechanics

#include <gtest/gtest.h>

// Temporarily not including header since all gameplay tests disabled
// #include "ghost-runner-tests.hpp"

// Minimal includes for config tests only
#include "game/ghost-runner/ghost-runner.hpp"

// ============================================
// GHOST RUNNER TESTS — TEMPORARILY DISABLED
// ============================================

// Minimal test suite for basic sanity checks
class GhostRunnerTestSuite : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

// Config tests updated for new Memory Maze API
TEST_F(GhostRunnerTestSuite, EasyConfigPresets) {
    GhostRunnerConfig easy = GHOST_RUNNER_EASY;
    ASSERT_EQ(easy.cols, 5);
    ASSERT_EQ(easy.rows, 3);
    ASSERT_EQ(easy.rounds, 4);
    ASSERT_EQ(easy.lives, 3);
    ASSERT_EQ(easy.previewMazeMs, 4000);
    ASSERT_EQ(easy.previewTraceMs, 4000);
    ASSERT_EQ(easy.bonkFlashMs, 1000);
    ASSERT_EQ(easy.startRow, 0);
    ASSERT_EQ(easy.startCol, 0);
    ASSERT_EQ(easy.exitRow, 2);
    ASSERT_EQ(easy.exitCol, 4);
}

TEST_F(GhostRunnerTestSuite, HardConfigPresets) {
    GhostRunnerConfig hard = GHOST_RUNNER_HARD;
    ASSERT_EQ(hard.cols, 7);
    ASSERT_EQ(hard.rows, 5);
    ASSERT_EQ(hard.rounds, 6);
    ASSERT_EQ(hard.lives, 1);
    ASSERT_EQ(hard.previewMazeMs, 2500);
    ASSERT_EQ(hard.previewTraceMs, 3000);
    ASSERT_EQ(hard.bonkFlashMs, 500);
    ASSERT_EQ(hard.exitRow, 4);
    ASSERT_EQ(hard.exitCol, 6);
}

TEST_F(GhostRunnerTestSuite, SessionResetClearsState) {
    GhostRunnerSession session;
    session.cursorRow = 3;
    session.cursorCol = 4;
    session.stepsUsed = 10;
    session.livesRemaining = 0;
    session.score = 100;
    session.bonkCount = 5;
    session.currentRound = 3;
    session.mazeFlashActive = true;

    session.reset();

    ASSERT_EQ(session.cursorRow, 0);
    ASSERT_EQ(session.cursorCol, 0);
    ASSERT_EQ(session.currentDirection, DIR_RIGHT);
    ASSERT_EQ(session.stepsUsed, 0);
    ASSERT_EQ(session.livesRemaining, 3);
    ASSERT_EQ(session.score, 0);
    ASSERT_EQ(session.bonkCount, 0);
    ASSERT_EQ(session.currentRound, 0);
    ASSERT_EQ(session.solutionLength, 0);
    ASSERT_FALSE(session.mazeFlashActive);
}

// All gameplay tests disabled pending rewrite for maze mechanics
/*
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
*/
