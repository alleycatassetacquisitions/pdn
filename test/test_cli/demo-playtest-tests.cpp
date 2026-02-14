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

TEST_F(DemoPlaytestSuite, GhostRunnerEasyPlaytest) {
    ghostRunnerEasyPlaytest(this);
}

TEST_F(DemoPlaytestSuite, GhostRunnerHardPlaytest) {
    ghostRunnerHardPlaytest(this);
}

// ============================================
// SUMMARY TEST
// ============================================

TEST_F(DemoPlaytestSuite, PlaytestSummary) {
    demoPlaytestSummary(this);
}
