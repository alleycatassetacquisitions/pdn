//
// Demo Playtest Tests — Simulated player playtests for difficulty validation
//

#include <gtest/gtest.h>

#include "demo-playtest-tests.hpp"

// ============================================
// SIGNAL ECHO PLAYTEST TESTS
// ============================================

TEST_F(DemoPlaytestSuite, SignalEchoEasyPlaytest) {
    signalEchoEasyPlaytest(this);
}

TEST_F(DemoPlaytestSuite, SignalEchoHardPlaytest) {
    signalEchoHardPlaytest(this);
}

// ============================================
// GHOST RUNNER PLAYTEST TESTS
// ============================================

// REMOVED: Ghost Runner redesigned in Wave 18 (memory maze, PR #220).
// Old playtest functions were deleted — these registrations had no backing code.
// Re-add when new memory maze playtest simulations are written.

// ============================================
// SUMMARY TEST
// ============================================

// DISABLED: Summary depends on Ghost Runner playtests

TEST_F(DemoPlaytestSuite, DISABLED_PlaytestSummary) {
    demoPlaytestSummary(this);
}
