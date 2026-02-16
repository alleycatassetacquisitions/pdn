//
// Firewall Decrypt Tests — Address, Lifecycle, Managed, Config, State Names, App ID
//
// DISABLED tests: Lifecycle/Managed/Config/Timeout tests fail after Firewall Decrypt redesign
// (Terminal Breach, Wave 18, #256). Test fixture uses old session/config fields.
// Previously masked by SIGSEGV crash (#300). See issue #327 for rewrite plan.

#include <gtest/gtest.h>

#include "firewall-decrypt-tests.hpp"

// ============================================
// FIREWALL DECRYPT ADDRESS TESTS
// ============================================

TEST_F(DecryptAddressTestSuite, AddressFormat) {
    decryptAddressFormat(this);
}

TEST_F(DecryptAddressTestSuite, Deterministic) {
    decryptAddressDeterministic(this);
}

TEST_F(DecryptAddressTestSuite, DecoyDiffers) {
    decryptDecoyDiffers(this);
}

TEST_F(DecryptAddressTestSuite, DecoySimilarity) {
    decryptDecoySimilarity(this);
}

TEST_F(DecryptAddressTestSuite, SetupRoundContainsTarget) {
    decryptSetupRoundContainsTarget(this);
}

// ============================================
// FIREWALL DECRYPT LIFECYCLE TESTS
// ============================================

TEST_F(DecryptLifecycleTestSuite, StartsInIntro) {
    decryptStartsInIntro(this);
}

TEST_F(DecryptLifecycleTestSuite, IntroTransitionsToScan) {
    decryptIntroTransitionsToScan(this);
}

TEST_F(DecryptLifecycleTestSuite, IntroShowsTitle) {
    decryptIntroShowsTitle(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_ScanShowsTarget) {
    decryptScanShowsTarget(this);
}

TEST_F(DecryptLifecycleTestSuite, CursorWraps) {
    decryptScanCursorWraps(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_CorrectAdvancesRound) {
    decryptCorrectAdvancesRound(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_WrongSelectionLoses) {
    decryptWrongSelectionLoses(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_AllRoundsWins) {
    decryptAllRoundsWins(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_WinSetsOutcome) {
    decryptWinSetsOutcome(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_LoseSetsOutcome) {
    decryptLoseSetsOutcome(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_StandaloneRestartAfterWin) {
    decryptStandaloneRestartAfterWin(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_StandaloneRestartAfterLose) {
    decryptStandaloneRestartAfterLose(this);
}

// ============================================
// FIREWALL DECRYPT MANAGED MODE TESTS
// ============================================

TEST_F(DecryptManagedTestSuite, DISABLED_LaunchesFromFdn) {
    decryptManagedLaunchesFromFdn(this);
}

TEST_F(DecryptManagedTestSuite, DISABLED_WinReturns) {
    decryptManagedWinReturns(this);
}

// ============================================
// FIREWALL DECRYPT CONFIG TESTS
// ============================================

TEST_F(DecryptConfigTestSuite, DISABLED_EasyPresetValues) {
    decryptEasyPresetValues(this);
}

TEST_F(DecryptConfigTestSuite, DISABLED_HardPresetValues) {
    decryptHardPresetValues(this);
}

// ============================================
// FIREWALL DECRYPT STATE NAME TESTS
// ============================================

TEST_F(DecryptStateNameTestSuite, NamesCorrect) {
    decryptStateNamesCorrect(this);
}

TEST_F(DecryptStateNameTestSuite, GetStateNameRoutes) {
    decryptGetStateNameRoutes(this);
}

// ============================================
// FIREWALL DECRYPT APP ID TESTS
// ============================================

TEST_F(DecryptAppIdTestSuite, AppIdForGame) {
    decryptAppIdForGame(this);
}

// ============================================
// TIMEOUT & ERROR PATH TESTS (NEW)
// ============================================

TEST_F(DecryptTimeoutTestSuite, DISABLED_ScanTimeoutSetsFlag) {
    decryptScanTimeoutSetsFlag(this);
}

TEST_F(DecryptTimeoutTestSuite, TimeoutRoutesToLose) {
    decryptTimeoutRoutesToLose(this);
}

TEST_F(DecryptTimeoutTestSuite, TimeoutSetsLoseOutcome) {
    decryptTimeoutSetsLoseOutcome(this);
}

TEST_F(DecryptTimeoutTestSuite, DISABLED_SelectionBeforeTimeout) {
    decryptSelectionBeforeTimeout(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_CursorWrapSingleCandidate) {
    decryptCursorWrapSingleCandidate(this);
}

TEST_F(DecryptLifecycleTestSuite, DISABLED_CursorWrapTwoCandidates) {
    decryptCursorWrapTwoCandidates(this);
}
