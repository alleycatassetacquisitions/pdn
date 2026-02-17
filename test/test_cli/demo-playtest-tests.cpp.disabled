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

// DISABLED: Ghost Runner redesigned in Wave 18 (memory maze, PR #220).
// These playtest simulations use old rhythm game API (ghostSpeedMs, targetZoneStart, etc.)
// and need complete rewrite for new memory maze mechanics.

TEST_F(DemoPlaytestSuite, DISABLED_GhostRunnerEasyPlaytest) {
    ghostRunnerEasyPlaytest(this);
}

TEST_F(DemoPlaytestSuite, DISABLED_GhostRunnerHardPlaytest) {
    ghostRunnerHardPlaytest(this);
}

// ============================================
// SUMMARY TEST
// ============================================

// DISABLED: Summary depends on Ghost Runner playtests

TEST_F(DemoPlaytestSuite, DISABLED_PlaytestSummary) {
    demoPlaytestSummary(this);
}
