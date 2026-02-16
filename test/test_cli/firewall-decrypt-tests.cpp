//
// Firewall Decrypt Tests — Address, Lifecycle, Managed, Config, State Names, App ID
//

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

TEST_F(DecryptLifecycleTestSuite, ScanShowsTarget) {
    decryptScanShowsTarget(this);
}

TEST_F(DecryptLifecycleTestSuite, CursorWraps) {
    decryptScanCursorWraps(this);
}

TEST_F(DecryptLifecycleTestSuite, CorrectAdvancesRound) {
    decryptCorrectAdvancesRound(this);
}

TEST_F(DecryptLifecycleTestSuite, WrongSelectionLoses) {
    decryptWrongSelectionLoses(this);
}

TEST_F(DecryptLifecycleTestSuite, AllRoundsWins) {
    decryptAllRoundsWins(this);
}

TEST_F(DecryptLifecycleTestSuite, WinSetsOutcome) {
    decryptWinSetsOutcome(this);
}

TEST_F(DecryptLifecycleTestSuite, LoseSetsOutcome) {
    decryptLoseSetsOutcome(this);
}

TEST_F(DecryptLifecycleTestSuite, StandaloneRestartAfterWin) {
    decryptStandaloneRestartAfterWin(this);
}

TEST_F(DecryptLifecycleTestSuite, StandaloneRestartAfterLose) {
    decryptStandaloneRestartAfterLose(this);
}

// ============================================
// FIREWALL DECRYPT MANAGED MODE TESTS
// ============================================

TEST_F(DecryptManagedTestSuite, LaunchesFromFdn) {
    decryptManagedLaunchesFromFdn(this);
}

TEST_F(DecryptManagedTestSuite, WinReturns) {
    decryptManagedWinReturns(this);
}

// ============================================
// FIREWALL DECRYPT CONFIG TESTS
// ============================================

TEST_F(DecryptConfigTestSuite, EasyPresetValues) {
    decryptEasyPresetValues(this);
}

TEST_F(DecryptConfigTestSuite, HardPresetValues) {
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

TEST_F(DecryptTimeoutTestSuite, ScanTimeoutSetsFlag) {
    decryptScanTimeoutSetsFlag(this);
}

TEST_F(DecryptTimeoutTestSuite, TimeoutRoutesToLose) {
    decryptTimeoutRoutesToLose(this);
}

TEST_F(DecryptTimeoutTestSuite, TimeoutSetsLoseOutcome) {
    decryptTimeoutSetsLoseOutcome(this);
}

TEST_F(DecryptTimeoutTestSuite, SelectionBeforeTimeout) {
    decryptSelectionBeforeTimeout(this);
}

TEST_F(DecryptLifecycleTestSuite, CursorWrapSingleCandidate) {
    decryptCursorWrapSingleCandidate(this);
}

TEST_F(DecryptLifecycleTestSuite, CursorWrapTwoCandidates) {
    decryptCursorWrapTwoCandidates(this);
}
