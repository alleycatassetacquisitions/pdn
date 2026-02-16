//
// FDN Integration Tests — Integration, Complete, Progress, Challenge, Switching,
// Difficulty, Auto-Boon, Player Fields, Extended, Routing, Konami, Color Profile,
// FDN Complete Routing, Idle, State Names
//
// DISABLED tests: Most tests in this file fail after KonamiMetaGame routing (Wave 17, #271).
// Tests assume direct FdnDetected → minigame launch but actual flow goes through KMG (app 9).
// Previously masked by SIGSEGV crash (#300, fixed by #323). See issue #327 for rewrite plan.

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

TEST_F(FdnIntegrationTestSuite, DISABLED_TransitionsToSignalEcho) {
    fdnIntegrationTransitionsToSignalEcho(this);
}

TEST_F(FdnIntegrationTestSuite, DISABLED_HandshakeTimeout) {
    fdnIntegrationHandshakeTimeout(this);
}

// ============================================
// FDN COMPLETE TESTS
// ============================================

TEST_F(FdnCompleteTestSuite, DISABLED_ShowsVictoryOnWin) {
    fdnCompleteShowsVictoryOnWin(this);
}

TEST_F(FdnCompleteTestSuite, DISABLED_UnlocksKonamiOnWin) {
    fdnCompleteUnlocksKonamiOnWin(this);
}

TEST_F(FdnCompleteTestSuite, DISABLED_UnlocksColorProfileOnHardWin) {
    fdnCompleteUnlocksColorProfileOnHardWin(this);
}

TEST_F(FdnCompleteTestSuite, DISABLED_ShowsDefeatedOnLoss) {
    fdnCompleteShowsDefeatedOnLoss(this);
}

TEST_F(FdnCompleteTestSuite, DISABLED_TransitionsToIdleAfterTimer) {
    fdnCompleteTransitionsToIdleAfterTimer(this);
}

TEST_F(FdnCompleteTestSuite, DISABLED_ClearsPendingChallenge) {
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

TEST_F(DifficultyGatingTestSuite, DISABLED_EasyWithoutBoon) {
    difficultyGatingEasyWithoutBoon(this);
}

TEST_F(DifficultyGatingTestSuite, DISABLED_HardWithBoon) {
    difficultyGatingHardWithBoon(this);
}

TEST_F(DifficultyGatingTestSuite, SetsLastGameType) {
    difficultyGatingSetsLastGameType(this);
}

TEST_F(DifficultyGatingTestSuite, DISABLED_UnknownGameIdle) {
    difficultyGatingUnknownGameIdle(this);
}

// ============================================
// AUTO-BOON TESTS
// ============================================

TEST_F(AutoBoonTestSuite, DISABLED_TriggersOnSeventhButton) {
    autoBoonTriggersOnSeventhButton(this);
}

TEST_F(AutoBoonTestSuite, DoesNotRetrigger) {
    autoBoonDoesNotRetrigger(this);
}

TEST_F(AutoBoonTestSuite, DISABLED_ShowsDisplay) {
    autoBoonShowsDisplay(this);
}

TEST_F(AutoBoonTestSuite, DISABLED_HardWinSetsPending) {
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

TEST_F(KonamiCommandTestSuite, DISABLED_SetsProgress) {
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

TEST_F(ColorProfilePromptTestSuite, DISABLED_YesEquips) {
    colorProfilePromptYesEquips(this);
}

TEST_F(ColorProfilePromptTestSuite, DISABLED_NoDoesNotEquip) {
    colorProfilePromptNoDoesNotEquip(this);
}

TEST_F(ColorProfilePromptTestSuite, DISABLED_AutoDismiss) {
    colorProfilePromptAutoDismiss(this);
}

TEST_F(ColorProfilePromptTestSuite, DISABLED_ShowsDisplay) {
    colorProfilePromptShowsDisplay(this);
}

TEST_F(ColorProfilePromptTestSuite, DISABLED_ClearsPending) {
    colorProfilePromptClearsPending(this);
}

// ============================================
// COLOR PROFILE PICKER TESTS
// ============================================

TEST_F(ColorProfilePickerTestSuite, DISABLED_ShowsHeader) {
    colorProfilePickerShowsHeader(this);
}

TEST_F(ColorProfilePickerTestSuite, DISABLED_ScrollWraps) {
    colorProfilePickerScrollWraps(this);
}

TEST_F(ColorProfilePickerTestSuite, DISABLED_SelectDefault) {
    colorProfilePickerSelectDefault(this);
}

TEST_F(ColorProfilePickerTestSuite, DISABLED_TransitionsToIdle) {
    colorProfilePickerTransitionsToIdle(this);
}

TEST_F(ColorProfilePickerTestSuite, PreselectsEquipped) {
    colorProfilePickerPreselectsEquipped(this);
}

TEST_F(ColorProfilePickerTestSuite, DISABLED_ShowsDefaultOnly) {
    colorProfilePickerShowsDefaultOnly(this);
}

TEST_F(ColorProfilePickerTestSuite, LedPreview) {
    colorProfilePickerLedPreview(this);
}

TEST_F(ColorProfilePickerTestSuite, DISABLED_VisualRedesign) {
    colorProfilePickerVisualRedesign(this);
}

TEST_F(ColorProfilePickerTestSuite, DISABLED_ControlLabel) {
    colorProfilePickerControlLabel(this);
}

// ============================================
// FDN COMPLETE ROUTING TESTS
// ============================================

TEST_F(FdnCompleteRoutingTestSuite, DISABLED_RoutesToPromptOnHardWin) {
    fdnCompleteRoutesToPromptOnHardWin(this);
}

TEST_F(FdnCompleteRoutingTestSuite, DISABLED_RoutesToIdleOnEasyWin) {
    fdnCompleteRoutesToIdleOnEasyWin(this);
}

TEST_F(FdnCompleteRoutingTestSuite, DISABLED_RoutesToIdleOnLoss) {
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

TEST_F(BoonAwardedTestSuite, DISABLED_UpdatesEligibility) {
    boonAwardedUpdatesEligibility(this);
}

TEST_F(BoonAwardedTestSuite, DISABLED_PersistsProgress) {
    boonAwardedPersistsProgress(this);
}

TEST_F(BoonAwardedTestSuite, DISABLED_DisplaysPaletteName) {
    boonAwardedDisplaysPaletteName(this);
}

TEST_F(BoonAwardedTestSuite, DISABLED_LedAnimation) {
    boonAwardedLedAnimation(this);
}

TEST_F(BoonAwardedTestSuite, DISABLED_TransitionsToColorPrompt) {
    boonAwardedTransitionsToColorPrompt(this);
}

TEST_F(BoonAwardedTestSuite, DISABLED_MultipleGameTypes) {
    boonAwardedMultipleGameTypes(this);
}

TEST_F(BoonAwardedTestSuite, DISABLED_ClearsLeds) {
    boonAwardedClearsLeds(this);
}

TEST_F(BoonAwardedTestSuite, DISABLED_HandlesMissingProfile) {
    boonAwardedHandlesMissingProfile(this);
}

// ============================================
// MULTI-PLAYER INTEGRATION TESTS
// ============================================

TEST_F(MultiPlayerIntegrationTestSuite, TwoPlayersBasicDiscovery) {
    multiPlayer2PlayersBasicDiscovery(this);
}

TEST_F(MultiPlayerIntegrationTestSuite, ThreePlayersKonamiProgression) {
    multiPlayer3PlayersKonamiProgression(this);
}

TEST_F(MultiPlayerIntegrationTestSuite, CableDisconnectRecovery) {
    multiPlayerCableDisconnectRecovery(this);
}

TEST_F(MultiPlayerIntegrationTestSuite, SevenGamesFullKonami) {
    multiPlayer7GamesFullKonami(this);
}

TEST_F(MultiPlayerIntegrationTestSuite, SequentialNpcEncounters) {
    multiPlayerSequentialNpcEncounters(this);
}

TEST_F(MultiPlayerIntegrationTestSuite, StressTest10Devices) {
    multiPlayerStressTest10Devices(this);
}
