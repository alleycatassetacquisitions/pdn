//
// E2E Game Suite Tests — E2E tests for 5 new minigames
//

#include <gtest/gtest.h>

#include "e2e-game-suite-tests.hpp"

// ============================================
// GHOST RUNNER E2E TESTS
// ============================================

TEST_F(E2EGameSuiteTestSuite, GhostRunnerEasyWin) {
    e2eGhostRunnerEasyWin(this);
}

// TODO: Fix Hard/Loss tests - see notes in e2e-game-suite-tests.hpp
// TEST_F(E2EGameSuiteTestSuite, GhostRunnerHardWin) {
//     e2eGhostRunnerHardWin(this);
// }

// TEST_F(E2EGameSuiteTestSuite, GhostRunnerLoss) {
//     e2eGhostRunnerLoss(this);
// }

// ============================================
// SPIKE VECTOR E2E TESTS
// ============================================

TEST_F(E2EGameSuiteTestSuite, SpikeVectorEasyWin) {
    e2eSpikeVectorEasyWin(this);
}

// TEST_F(E2EGameSuiteTestSuite, SpikeVectorHardWin) {
//     e2eSpikeVectorHardWin(this);
// }

// TEST_F(E2EGameSuiteTestSuite, SpikeVectorLoss) {
//     e2eSpikeVectorLoss(this);
// }

// ============================================
// CIPHER PATH E2E TESTS
// ============================================

TEST_F(E2EGameSuiteTestSuite, CipherPathEasyWin) {
    e2eCipherPathEasyWin(this);
}

// TEST_F(E2EGameSuiteTestSuite, CipherPathHardWin) {
//     e2eCipherPathHardWin(this);
// }

// TEST_F(E2EGameSuiteTestSuite, CipherPathLoss) {
//     e2eCipherPathLoss(this);
// }

// ============================================
// EXPLOIT SEQUENCER E2E TESTS
// ============================================

TEST_F(E2EGameSuiteTestSuite, ExploitSequencerEasyWin) {
    e2eExploitSequencerEasyWin(this);
}

// TEST_F(E2EGameSuiteTestSuite, ExploitSequencerHardWin) {
//     e2eExploitSequencerHardWin(this);
// }

// TEST_F(E2EGameSuiteTestSuite, ExploitSequencerLoss) {
//     e2eExploitSequencerLoss(this);
// }

// ============================================
// BREACH DEFENSE E2E TESTS
// ============================================

TEST_F(E2EGameSuiteTestSuite, BreachDefenseEasyWin) {
    e2eBreachDefenseEasyWin(this);
}

// TEST_F(E2EGameSuiteTestSuite, BreachDefenseHardWin) {
//     e2eBreachDefenseHardWin(this);
// }

// TEST_F(E2EGameSuiteTestSuite, BreachDefenseLoss) {
//     e2eBreachDefenseLoss(this);
// }
