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

// Note: Hard/Loss edge cases are covered in hard-mode-reencounter-tests.hpp
// Basic hard win flow is tested in progression-e2e-tests.hpp
// Basic loss flow is tested in progression-e2e-tests.hpp

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
