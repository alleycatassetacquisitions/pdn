//
// FDN Integration Tests — Integration, Complete, Progress, Challenge, Switching,
// Difficulty, Auto-Boon, Player Fields, Extended, Routing, Konami, Color Profile,
// FDN Complete Routing, Idle, State Names
//

#include <gtest/gtest.h>

#include "fdn-integration-tests.hpp"
#include "progression-core-tests.hpp"
#include "color-profile-tests.hpp"
#include "boon-awarded-tests.hpp"

// ============================================
// FDN INTEGRATION TESTS
// ============================================

TEST_F(FdnIntegrationTestSuite, DetectsFdnBroadcast) {
    fdnIntegrationDetectsFdnBroadcast(this);
}

TEST_F(FdnIntegrationTestSuite, HandshakeSendsMAC) {
    fdnIntegrationHandshakeSendsMAC(this);
}

TEST_F(FdnIntegrationTestSuite, TransitionsToSignalEcho) {
    fdnIntegrationTransitionsToSignalEcho(this);
}

TEST_F(FdnIntegrationTestSuite, HandshakeTimeout) {
    fdnIntegrationHandshakeTimeout(this);
}

// ============================================
// FDN COMPLETE TESTS
// ============================================

TEST_F(FdnCompleteTestSuite, ShowsVictoryOnWin) {
    fdnCompleteShowsVictoryOnWin(this);
}

TEST_F(FdnCompleteTestSuite, UnlocksKonamiOnWin) {
    fdnCompleteUnlocksKonamiOnWin(this);
}

TEST_F(FdnCompleteTestSuite, UnlocksColorProfileOnHardWin) {
    fdnCompleteUnlocksColorProfileOnHardWin(this);
}

TEST_F(FdnCompleteTestSuite, ShowsDefeatedOnLoss) {
    fdnCompleteShowsDefeatedOnLoss(this);
}

TEST_F(FdnCompleteTestSuite, TransitionsToIdleAfterTimer) {
    fdnCompleteTransitionsToIdleAfterTimer(this);
}

TEST_F(FdnCompleteTestSuite, ClearsPendingChallenge) {
    fdnCompleteClearsPendingChallenge(this);
}

// ============================================
// PROGRESS MANAGER TESTS
// ============================================

TEST_F(ProgressManagerTestSuite, SavesOnWin) {
    progressManagerSavesOnWin(this);
}

TEST_F(ProgressManagerTestSuite, LoadsProgress) {
    progressManagerLoadsProgress(this);
}

TEST_F(ProgressManagerTestSuite, ClearsProgress) {
    progressManagerClearsProgress(this);
}

// ============================================
// PLAYER CHALLENGE TESTS
// ============================================

TEST_F(PlayerChallengeTestSuite, KonamiUnlockAndQuery) {
    playerKonamiUnlockAndQuery(this);
}

TEST_F(PlayerChallengeTestSuite, PendingChallengeSetClear) {
    playerPendingChallengeSetClear(this);
}

TEST_F(PlayerChallengeTestSuite, ColorProfileEligibility) {
    playerColorProfileEligibility(this);
}

TEST_F(PlayerChallengeTestSuite, EquippedColorProfile) {
    playerEquippedColorProfile(this);
}

TEST_F(PlayerChallengeTestSuite, KonamiProgressSetGet) {
    playerKonamiProgressSetGet(this);
}

// ============================================
// APP SWITCHING TESTS
// ============================================

TEST_F(AppSwitchingTestSuite, SignalEchoRegistered) {
    appSwitchingSignalEchoRegistered(this);
}

TEST_F(AppSwitchingTestSuite, QuickdrawActiveAtStart) {
    appSwitchingQuickdrawActiveAtStart(this);
}

TEST_F(AppSwitchingTestSuite, ReturnToPrevious) {
    appSwitchingReturnToPrevious(this);
}

// ============================================
// DIFFICULTY GATING TESTS
// ============================================

TEST_F(DifficultyGatingTestSuite, EasyWithoutBoon) {
    difficultyGatingEasyWithoutBoon(this);
}

TEST_F(DifficultyGatingTestSuite, HardWithBoon) {
    difficultyGatingHardWithBoon(this);
}

TEST_F(DifficultyGatingTestSuite, SetsLastGameType) {
    difficultyGatingSetsLastGameType(this);
}

TEST_F(DifficultyGatingTestSuite, UnknownGameIdle) {
    difficultyGatingUnknownGameIdle(this);
}

// ============================================
// AUTO-BOON TESTS
// ============================================

TEST_F(AutoBoonTestSuite, TriggersOnSeventhButton) {
    autoBoonTriggersOnSeventhButton(this);
}

TEST_F(AutoBoonTestSuite, DoesNotRetrigger) {
    autoBoonDoesNotRetrigger(this);
}

TEST_F(AutoBoonTestSuite, ShowsDisplay) {
    autoBoonShowsDisplay(this);
}

TEST_F(AutoBoonTestSuite, HardWinSetsPending) {
    autoBoonHardWinSetsPending(this);
}

TEST_F(AutoBoonTestSuite, EasyWinNoPending) {
    autoBoonEasyWinNoPending(this);
}

// ============================================
// PLAYER FIELD ACCESSOR TESTS (extended)
// ============================================

TEST_F(PlayerChallengeTestSuite, IsKonamiComplete) {
    playerIsKonamiComplete(this);
}

TEST_F(PlayerChallengeTestSuite, KonamiBoonSetGet) {
    playerKonamiBoonSetGet(this);
}

TEST_F(PlayerChallengeTestSuite, LastFdnGameTypeSetGet) {
    playerLastFdnGameTypeSetGet(this);
}

TEST_F(PlayerChallengeTestSuite, PendingProfileGameSetGet) {
    playerPendingProfileGameSetGet(this);
}

TEST_F(PlayerChallengeTestSuite, ColorProfileEligibilitySet) {
    playerColorProfileEligibilitySet(this);
}

// ============================================
// PROGRESS MANAGER EXTENDED TESTS
// ============================================

TEST_F(ProgressManagerTestSuite, SaveLoadBoon) {
    progressManagerSaveLoadBoon(this);
}

TEST_F(ProgressManagerTestSuite, SaveLoadProfile) {
    progressManagerSaveLoadProfile(this);
}

TEST_F(ProgressManagerTestSuite, SaveLoadEligibility) {
    progressManagerSaveLoadEligibility(this);
}

TEST_F(ProgressManagerTestSuite, ClearAll) {
    progressManagerClearAll(this);
}

TEST_F(ProgressManagerTestSuite, SyncCallsServer) {
    progressManagerSyncCallsServer(this);
}

// ============================================
// GAME ROUTING TESTS
// ============================================

TEST_F(GameRoutingTestSuite, AppIdForGame) {
    gameRoutingSignalEcho(this);
}

// ============================================
// KONAMI COMMAND TESTS
// ============================================

TEST_F(KonamiCommandTestSuite, ShowsProgress) {
    konamiCommandShowsProgress(this);
}

TEST_F(KonamiCommandTestSuite, SetsProgress) {
    konamiCommandSetsProgress(this);
}

// ============================================
// COLOR PROFILE LOOKUP TESTS
// ============================================

TEST_F(ColorProfileLookupTestSuite, SignalEcho) {
    colorProfileLookupSignalEcho(this);
}

TEST_F(ColorProfileLookupTestSuite, FirewallDecrypt) {
    colorProfileLookupFirewallDecrypt(this);
}

TEST_F(ColorProfileLookupTestSuite, Default) {
    colorProfileLookupDefault(this);
}

TEST_F(ColorProfileLookupTestSuite, Names) {
    colorProfileLookupNames(this);
}

// ============================================
// COLOR PROFILE PROMPT TESTS
// ============================================

TEST_F(ColorProfilePromptTestSuite, YesEquips) {
    colorProfilePromptYesEquips(this);
}

TEST_F(ColorProfilePromptTestSuite, NoDoesNotEquip) {
    colorProfilePromptNoDoesNotEquip(this);
}

TEST_F(ColorProfilePromptTestSuite, AutoDismiss) {
    colorProfilePromptAutoDismiss(this);
}

TEST_F(ColorProfilePromptTestSuite, ShowsDisplay) {
    colorProfilePromptShowsDisplay(this);
}

TEST_F(ColorProfilePromptTestSuite, ClearsPending) {
    colorProfilePromptClearsPending(this);
}

// ============================================
// COLOR PROFILE PICKER TESTS
// ============================================

TEST_F(ColorProfilePickerTestSuite, ShowsHeader) {
    colorProfilePickerShowsHeader(this);
}

TEST_F(ColorProfilePickerTestSuite, ScrollWraps) {
    colorProfilePickerScrollWraps(this);
}

TEST_F(ColorProfilePickerTestSuite, SelectDefault) {
    colorProfilePickerSelectDefault(this);
}

TEST_F(ColorProfilePickerTestSuite, TransitionsToIdle) {
    colorProfilePickerTransitionsToIdle(this);
}

TEST_F(ColorProfilePickerTestSuite, PreselectsEquipped) {
    colorProfilePickerPreselectsEquipped(this);
}

// ============================================
// FDN COMPLETE ROUTING TESTS
// ============================================

TEST_F(FdnCompleteRoutingTestSuite, RoutesToPromptOnHardWin) {
    fdnCompleteRoutesToPromptOnHardWin(this);
}

TEST_F(FdnCompleteRoutingTestSuite, RoutesToIdleOnEasyWin) {
    fdnCompleteRoutesToIdleOnEasyWin(this);
}

TEST_F(FdnCompleteRoutingTestSuite, RoutesToIdleOnLoss) {
    fdnCompleteRoutesToIdleOnLoss(this);
}

// ============================================
// IDLE COLOR PICKER ENTRY TESTS
// ============================================

TEST_F(IdleColorPickerTestSuite, LongPressEntersPicker) {
    idleLongPressEntersPicker(this);
}

TEST_F(IdleColorPickerTestSuite, LongPressNoPickerWithoutEligibility) {
    idleLongPressNoPickerWithoutEligibility(this);
}

// ============================================
// STATE NAME TESTS
// ============================================

TEST_F(StateNameTestSuite, ColorProfilePrompt) {
    stateNameColorProfilePrompt(this);
}

TEST_F(StateNameTestSuite, ColorProfilePicker) {
    stateNameColorProfilePicker(this);
}

// ============================================
// BOON AWARDED TESTS
// ============================================

TEST_F(BoonAwardedTestSuite, UpdatesEligibility) {
    boonAwardedUpdatesEligibility(this);
}

TEST_F(BoonAwardedTestSuite, PersistsProgress) {
    boonAwardedPersistsProgress(this);
}

TEST_F(BoonAwardedTestSuite, DisplaysPaletteName) {
    boonAwardedDisplaysPaletteName(this);
}

TEST_F(BoonAwardedTestSuite, LedAnimation) {
    boonAwardedLedAnimation(this);
}

TEST_F(BoonAwardedTestSuite, TransitionsToColorPrompt) {
    boonAwardedTransitionsToColorPrompt(this);
}

TEST_F(BoonAwardedTestSuite, MultipleGameTypes) {
    boonAwardedMultipleGameTypes(this);
}

TEST_F(BoonAwardedTestSuite, ClearsLeds) {
    boonAwardedClearsLeds(this);
}

TEST_F(BoonAwardedTestSuite, HandlesMissingProfile) {
    boonAwardedHandlesMissingProfile(this);
}
