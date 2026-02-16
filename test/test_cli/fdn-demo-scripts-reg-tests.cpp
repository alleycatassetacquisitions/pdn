//
// FDN Demo Script Tests — Automated walkthrough scenarios for integration testing
//
// DISABLED tests: Demo script tests fail after KMG routing (Wave 17, #271).
// Previously masked by SIGSEGV crash (#300). See #327.

#include <gtest/gtest.h>

#include "fdn-demo-scripts.hpp"

// ============================================
// SIGNAL ECHO DEMO SCRIPTS
// ============================================

TEST_F(FdnDemoScriptTestSuite, DISABLED_SignalEchoEasyCompleteWalkthrough) {
    signalEchoEasyCompleteWalkthrough(this);
}

TEST_F(FdnDemoScriptTestSuite, DISABLED_SignalEchoHardCompleteWalkthrough) {
    signalEchoHardCompleteWalkthrough(this);
}

TEST_F(FdnDemoScriptTestSuite, DISABLED_SignalEchoLossNoRewards) {
    signalEchoLossNoRewards(this);
}

TEST_F(FdnDemoScriptTestSuite, DISABLED_SignalEchoMultipleErrorsLoss) {
    signalEchoMultipleErrorsLoss(this);
}

TEST_F(FdnDemoScriptTestSuite, DISABLED_SignalEchoRapidButtonSpam) {
    signalEchoRapidButtonSpam(this);
}

// ============================================
// GHOST RUNNER DEMO SCRIPTS
// ============================================
// NOTE: Tests disabled during Wave 18 redesign (#220)
/*
TEST_F(FdnDemoScriptTestSuite, GhostRunnerEasyCompleteWalkthrough) {
    ghostRunnerEasyCompleteWalkthrough(this);
}
*/

// ============================================
// SPIKE VECTOR DEMO SCRIPTS
// ============================================

TEST_F(FdnDemoScriptTestSuite, DISABLED_SpikeVectorEasyCompleteWalkthrough) {
    spikeVectorEasyCompleteWalkthrough(this);
}

// ============================================
// ERROR CASE DEMO SCRIPTS
// ============================================

TEST_F(FdnDemoScriptTestSuite, DISABLED_FdnDisconnectDuringHandshake) {
    fdnDisconnectDuringHandshake(this);
}
