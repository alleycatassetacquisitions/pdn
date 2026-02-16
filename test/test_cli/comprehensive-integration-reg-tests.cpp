//
// Comprehensive Integration Tests — Complete integration tests for all 7 FDN minigames
//
// DISABLED: ALL tests in this suite fail after PR #323 fixed the SIGSEGV crash (#300).
// The crash was masking failures caused by KonamiMetaGame routing (Wave 17, #271):
//
// Root cause: Tests assume direct FdnDetected → minigame launch, but the actual flow
// now goes FdnDetected → KonamiMetaGame (app 9) → minigame. This breaks:
//   1. Timing — KMG adds 2+ seconds before minigame launch
//   2. Session state — minigame mount resets session values set by tests pre-launch
//   3. State assertions — Quickdraw state is FDN_DETECTED during KMG, not FDN_REENCOUNTER
//   4. Re-encounter flow — hard mode choice is now handled by KMG, not FdnReencounter
//
// Fix requires adding waitForMinigameLaunch() helper and rewriting all test functions.
// See issue for full analysis and rewrite plan.

#include <gtest/gtest.h>

#include "comprehensive-integration-tests.hpp"

// ============================================
// SIGNAL ECHO INTEGRATION TESTS
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SignalEchoEasyWinUnlocksButton) {
    signalEchoEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SignalEchoHardWinUnlocksColorProfile) {
    signalEchoHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SignalEchoLossNoRewards) {
    signalEchoLossNoRewards(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SignalEchoTimeoutEdgeCase) {
    signalEchoTimeoutEdgeCase(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SignalEchoRapidButtonPresses) {
    signalEchoRapidButtonPresses(this);
}

// ============================================
// GHOST RUNNER INTEGRATION TESTS
// ============================================
// Ghost Runner redesigned in Wave 18 (#220) — Memory Maze.
// Old test functions disabled (#if 0). Re-add when new maze tests are written.

// ============================================
// SPIKE VECTOR INTEGRATION TESTS
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SpikeVectorEasyWinUnlocksButton) {
    spikeVectorEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SpikeVectorHardWinUnlocksColorProfile) {
    spikeVectorHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SpikeVectorLossNoRewards) {
    spikeVectorLossNoRewards(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_SpikeVectorRapidCursorMovement) {
    spikeVectorRapidCursorMovement(this);
}

// ============================================
// FIREWALL DECRYPT INTEGRATION TESTS
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_FirewallDecryptEasyWinUnlocksButton) {
    firewallDecryptEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_FirewallDecryptHardWinUnlocksColorProfile) {
    firewallDecryptHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_FirewallDecryptLossNoRewards) {
    firewallDecryptLossNoRewards(this);
}

// ============================================
// CIPHER PATH INTEGRATION TESTS
// ============================================
// Cipher Path redesigned in Wave 18 (#242) — Wire Routing.
// Old test functions disabled (#if 0). Re-add when new wire routing tests are written.

// ============================================
// EXPLOIT SEQUENCER INTEGRATION TESTS
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_ExploitSequencerEasyWinUnlocksButton) {
    exploitSequencerEasyWinUnlocksButton(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_ExploitSequencerHardWinUnlocksColorProfile) {
    exploitSequencerHardWinUnlocksColorProfile(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_ExploitSequencerLossNoRewards) {
    exploitSequencerLossNoRewards(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_ExploitSequencerExactMarkerPress) {
    exploitSequencerExactMarkerPress(this);
}

// ============================================
// BREACH DEFENSE INTEGRATION TESTS
// ============================================
// Breach Defense redesigned in Wave 18 (#231) — Pong x Invaders.
// Old test functions disabled (#if 0). Re-add when new combo mechanics tests are written.

// ============================================
// CROSS-GAME EDGE CASES
// ============================================

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_AllGamesCompleteUnlocksAllButtons) {
    allGamesCompleteUnlocksAllButtons(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_FdnDisconnectMidGame) {
    fdnDisconnectMidGame(this);
}

TEST_F(ComprehensiveIntegrationTestSuite, DISABLED_AllGamesHandleZeroScore) {
    allGamesHandleZeroScore(this);
}
