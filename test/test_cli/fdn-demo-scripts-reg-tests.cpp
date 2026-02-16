//
// FDN Demo Script Tests — Automated walkthrough scenarios for integration testing
//

#include <gtest/gtest.h>

#include "fdn-demo-scripts.hpp"

// ============================================
// SIGNAL ECHO DEMO SCRIPTS
// ============================================

TEST_F(FdnDemoScriptTestSuite, SignalEchoEasyCompleteWalkthrough) {
    signalEchoEasyCompleteWalkthrough(this);
}

TEST_F(FdnDemoScriptTestSuite, SignalEchoHardCompleteWalkthrough) {
    signalEchoHardCompleteWalkthrough(this);
}

TEST_F(FdnDemoScriptTestSuite, SignalEchoLossNoRewards) {
    signalEchoLossNoRewards(this);
}

TEST_F(FdnDemoScriptTestSuite, SignalEchoMultipleErrorsLoss) {
    signalEchoMultipleErrorsLoss(this);
}

TEST_F(FdnDemoScriptTestSuite, SignalEchoRapidButtonSpam) {
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

TEST_F(FdnDemoScriptTestSuite, SpikeVectorEasyCompleteWalkthrough) {
    spikeVectorEasyCompleteWalkthrough(this);
}

// ============================================
// ERROR CASE DEMO SCRIPTS
// ============================================

TEST_F(FdnDemoScriptTestSuite, FdnDisconnectDuringHandshake) {
    fdnDisconnectDuringHandshake(this);
}
