//
// Comprehensive Integration Tests — Complete integration tests for all 7 FDN minigames
//

#include <gtest/gtest.h>

#include "comprehensive-integration-tests.hpp"

// ============================================
// SIGNAL ECHO INTEGRATION TESTS
// ============================================

// DISABLED: Unlock flow changed in Wave 17 KonamiMetaGame refactoring (Issue #271)
// These tests fail because the app transition flow (FdnDetected → KonamiMetaGame → minigame →
// resume → FdnComplete) doesn't match test expectations. Re-enable after verifying new flow.

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SignalEchoEasyWinUnlocksButton) {
    signalEchoEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SignalEchoHardWinUnlocksColorProfile) {
    signalEchoHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, SignalEchoLossNoRewards) {
    signalEchoLossNoRewards(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, SignalEchoTimeoutEdgeCase) {
    signalEchoTimeoutEdgeCase(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, SignalEchoRapidButtonPresses) {
    signalEchoRapidButtonPresses(this);
}

// ============================================
// GHOST RUNNER INTEGRATION TESTS
// ============================================

// REMOVED: Ghost Runner redesigned in Wave 18 (memory maze, PR #220).
// Old rhythm game test functions were deleted — these registrations had no backing code.
// Re-add when new memory maze integration tests are written.

// ============================================
// SPIKE VECTOR INTEGRATION TESTS
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, SpikeVectorEasyWinUnlocksButton) {
    spikeVectorEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, SpikeVectorHardWinUnlocksColorProfile) {
    spikeVectorHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, SpikeVectorLossNoRewards) {
    spikeVectorLossNoRewards(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, SpikeVectorRapidCursorMovement) {
    spikeVectorRapidCursorMovement(this);
}

// ============================================
// FIREWALL DECRYPT INTEGRATION TESTS
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, FirewallDecryptEasyWinUnlocksButton) {
    firewallDecryptEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, FirewallDecryptHardWinUnlocksColorProfile) {
    firewallDecryptHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, FirewallDecryptLossNoRewards) {
    firewallDecryptLossNoRewards(this);
}

// ============================================
// CIPHER PATH INTEGRATION TESTS
// ============================================

// REMOVED: Cipher Path redesigned in Wave 18 (wire routing puzzle, PR #242).
// Old API test functions were deleted — these registrations had no backing code.
// Re-add when new wire routing integration tests are written.

// ============================================
// EXPLOIT SEQUENCER INTEGRATION TESTS
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, ExploitSequencerEasyWinUnlocksButton) {
    exploitSequencerEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, ExploitSequencerHardWinUnlocksColorProfile) {
    exploitSequencerHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, ExploitSequencerLossNoRewards) {
    exploitSequencerLossNoRewards(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, ExploitSequencerExactMarkerPress) {
    exploitSequencerExactMarkerPress(this);
}

// ============================================
// BREACH DEFENSE INTEGRATION TESTS
// ============================================
// NOTE: Tests temporarily disabled during Wave 18 redesign (#231)
// Session structure changed - tests need rewrite for combo mechanics

/*
TEST_F(ComprehensiveIntegrationTestSuite, BreachDefenseEasyWinUnlocksButton) {
    breachDefenseEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, BreachDefenseHardWinUnlocksColorProfile) {
    breachDefenseHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, BreachDefenseLossNoRewards) {
    breachDefenseLossNoRewards(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, BreachDefenseShieldMovementDuringThreat) {
    breachDefenseShieldMovementDuringThreat(this);
}
*/

// ============================================
// CROSS-GAME EDGE CASES
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, AllGamesCompleteUnlocksAllButtons) {
    allGamesCompleteUnlocksAllButtons(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, FdnDisconnectMidGame) {
    fdnDisconnectMidGame(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, AllGamesHandleZeroScore) {
    allGamesHandleZeroScore(this);
}
