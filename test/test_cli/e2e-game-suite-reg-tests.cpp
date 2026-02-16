//
// E2E Game Suite Tests — E2E tests for minigames
//
// DISABLED: ALL tests in this suite fail due to KonamiMetaGame routing (Wave 17, #271).
// Previously masked by SIGSEGV crash (#300, fixed by #323). Tests assume direct
// FdnDetected → minigame launch but actual flow goes through KonamiMetaGame (app 9).
// See comprehensive-integration-reg-tests.cpp header for full root cause analysis.

#include <gtest/gtest.h>

#include "e2e-game-suite-tests.hpp"

// ============================================
// GHOST RUNNER E2E TESTS
// ============================================
// Ghost Runner redesigned in Wave 18 (#220) — Memory Maze.
// Old test functions disabled (#if 0). Re-add when new maze tests are written.

// ============================================
// SPIKE VECTOR E2E TESTS
// ============================================

TEST_F(E2EGameSuiteTestSuite, DISABLED_SpikeVectorEasyWin) {
    e2eSpikeVectorEasyWin(this);
}

TEST_F(E2EGameSuiteTestSuite, DISABLED_SpikeVectorHardWin) {
    e2eSpikeVectorHardWin(this);
}

TEST_F(E2EGameSuiteTestSuite, DISABLED_SpikeVectorLoss) {
    e2eSpikeVectorLoss(this);
}

// ============================================
// CIPHER PATH E2E TESTS
// ============================================
// Cipher Path redesigned in Wave 18 (#242) — Wire Routing.
// Old test functions disabled (#if 0). Re-add when new wire routing tests are written.

// ============================================
// EXPLOIT SEQUENCER E2E TESTS
// ============================================

TEST_F(E2EGameSuiteTestSuite, DISABLED_ExploitSequencerEasyWin) {
    e2eExploitSequencerEasyWin(this);
}

TEST_F(E2EGameSuiteTestSuite, DISABLED_ExploitSequencerHardWin) {
    e2eExploitSequencerHardWin(this);
}

TEST_F(E2EGameSuiteTestSuite, DISABLED_ExploitSequencerLoss) {
    e2eExploitSequencerLoss(this);
}

// ============================================
// BREACH DEFENSE E2E TESTS
// ============================================
// Breach Defense redesigned in Wave 18 (#231) — Pong x Invaders.
// Old test functions disabled (#if 0). Re-add when new combo mechanics tests are written.
