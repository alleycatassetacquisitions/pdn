//
// Progression Edge Case Tests — Registration file
//

#include <gtest/gtest.h>

#include "progression-edge-tests.hpp"

// ============================================
// IDEMPOTENCY TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, UnlockSameButtonTwice) {
    unlockSameButtonTwice(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, AddSameEligibilityTwice) {
    addSameEligibilityTwice(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, DISABLED_IncrementAttemptsTwice) {
    incrementAttemptsTwice(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, SaveProgressTwice) {
    saveProgressTwice(this);
}

// ============================================
// PROGRESSION RESET TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, ClearProgressResetsAll) {
    clearProgressResetsAll(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, ClearProgressDuringGame) {
    clearProgressDuringGame(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, PartialResetViaReload) {
    partialResetViaReload(this);
}

// ============================================
// HARD MODE VS EASY MODE TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, DISABLED_HardEasyAttemptsSeparate) {
    hardEasyAttemptsSeparate(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, HardWinSetsEligibilityEasyDoesNot) {
    hardWinSetsEligibilityEasyDoesNot(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, DISABLED_SwitchingDifficultyPreservesProgress) {
    switchingDifficultyPreservesProgress(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, EasyAfterHardDoesNotDowngrade) {
    easyAfterHardDoesNotDowngrade(this);
}

// ============================================
// PERSISTENCE TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, ProgressPersistsAcrossSessions) {
    progressPersistsAcrossSessions(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, CorruptedDataRecoversGracefully) {
    corruptedDataRecoversGracefully(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, MissingFieldsUseDefaults) {
    missingFieldsUseDefaults(this);
}

// ============================================
// BOUNDARY TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, MaximumKonamiProgress) {
    maximumKonamiProgress(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, DISABLED_MaximumAttemptCounts) {
    maximumAttemptCounts(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, ZeroProgressionState) {
    zeroProgressionState(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, AllColorProfilesUnlocked) {
    allColorProfilesUnlocked(this);
}

// ============================================
// INVALID STATE RECOVERY TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, InvalidKonamiProgressHandled) {
    invalidKonamiProgressHandled(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, OutOfRangeGameTypeHandled) {
    outOfRangeGameTypeHandled(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, NegativeEquippedProfileHandled) {
    negativeEquippedProfileHandled(this);
}

// ============================================
// COLOR PROFILE UNLOCKING TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, UnlockColorProfileViaEligibility) {
    unlockColorProfileViaEligibility(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, UseLockedProfileHandled) {
    useLockedProfileHandled(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, ColorProfilePersistence) {
    colorProfilePersistence(this);
}

// ============================================
// SERVER MERGE CONFLICT RESOLUTION TESTS
// ============================================

TEST_F(ProgressionEdgeCaseTestSuite, ServerMergeBitmaskUnion) {
    serverMergeBitmaskUnion(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, DISABLED_ServerMergeMaxWins) {
    serverMergeMaxWins(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, ServerMergeMissingFieldsPreserveLocal) {
    serverMergeMissingFieldsPreserveLocal(this);
}

TEST_F(ProgressionEdgeCaseTestSuite, ServerMergeColorEligibilityUnion) {
    serverMergeColorEligibilityUnion(this);
}
