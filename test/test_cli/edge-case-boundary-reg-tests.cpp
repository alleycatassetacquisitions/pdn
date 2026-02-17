//
// Edge Case and Boundary Tests — Registration
// Tests for boundary values, rapid input, interrupted flows, difficulty scaling
//

#include <gtest/gtest.h>
#include "edge-case-boundary-tests.hpp"

// ============================================
// BOUNDARY VALUE TESTS
// ============================================

TEST_F(EdgeCaseBoundaryTestSuite, DifficultyLevelZero) {
    boundaryDifficultyLevelZero(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, DifficultyLevelMax) {
    boundaryDifficultyLevelMax(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, TimerZeroDuration) {
    boundaryTimerZeroDuration(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, TimerOneMillisecond) {
    boundaryTimerOneMillisecond(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, TimerMaxDuration) {
    boundaryTimerMaxDuration(this);
}

// ============================================
// RAPID INPUT STRESS TESTS
// ============================================

TEST_F(EdgeCaseBoundaryTestSuite, ButtonSpamDuringTransition) {
    rapidInputButtonSpamDuringTransition(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, SimultaneousButtons) {
    rapidInputSimultaneousButtons(this);
}

TEST_F(MinigameBoundaryTestSuite, ButtonDuringIntro) {
    rapidInputButtonDuringIntro(this);
}

TEST_F(MinigameBoundaryTestSuite, ButtonDuringOutcome) {
    rapidInputButtonDuringOutcome(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, AlternatingButtons) {
    rapidInputAlternatingButtons(this);
}

// ============================================
// INTERRUPTED FLOW TESTS
// ============================================

TEST_F(FdnBoundaryTestSuite, CableDisconnectDuringEval) {
    interruptedFlowCableDisconnectDuringEval(this);
}

TEST_F(FdnBoundaryTestSuite, RapidCableToggle) {
    interruptedFlowRapidCableToggle(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, NoNpcConnected) {
    interruptedFlowNoNpcConnected(this);
}

TEST_F(FdnBoundaryTestSuite, DisconnectAfterHandshake) {
    interruptedFlowDisconnectAfterHandshake(this);
}

TEST_F(FdnBoundaryTestSuite, ButtonDuringDisconnect) {
    interruptedFlowButtonDuringDisconnect(this);
}

// ============================================
// DIFFICULTY SCALER EDGE CASES
// ============================================

TEST_F(EdgeCaseBoundaryTestSuite, DifficultyAfterManyGames) {
    difficultyScalerAfterManyGames(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, DifficultyAllGamesLevelZero) {
    difficultyScalerAllGamesLevelZero(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, DifficultyAllGamesMaxLevel) {
    difficultyScalerAllGamesMaxLevel(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, DifficultyMonotonicProgression) {
    difficultyScalerMonotonicProgression(this);
}

// ============================================
// SERIAL PROTOCOL EDGE CASES
// ============================================

TEST_F(EdgeCaseBoundaryTestSuite, SerialEmptyMessage) {
    serialProtocolEmptyMessage(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, SerialLongMessage) {
    serialProtocolLongMessage(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, SerialMalformedFdn) {
    serialProtocolMalformedFdn(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, SerialMessageFlood) {
    serialProtocolMessageFlood(this);
}

TEST_F(EdgeCaseBoundaryTestSuite, SerialNullCharacter) {
    serialProtocolNullCharacter(this);
}
