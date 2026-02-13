//
// Progression E2E Tests — E2E, Re-encounter, Color Context, Button Swap, Palette, Bounty Role
//

#include <gtest/gtest.h>

#include "progression-e2e-tests.hpp"
#include "negative-flow-tests.hpp"

// ============================================
// E2E PROGRESSION TESTS
// ============================================

TEST_F(ProgressionE2ETestSuite, SignalEchoEasyWin) {
    e2eSignalEchoEasyWin(this);
}

TEST_F(ProgressionE2ETestSuite, AutoBoonOnSeventhButton) {
    e2eAutoBoonOnSeventhButton(this);
}

TEST_F(ProgressionE2ETestSuite, ServerSyncOnWin) {
    e2eServerSyncOnWin(this);
}

TEST_F(ProgressionE2ETestSuite, HardModeColorEquip) {
    e2eHardModeColorEquip(this);
}

TEST_F(ProgressionE2ETestSuite, ColorPromptDecline) {
    e2eColorPromptDecline(this);
}

TEST_F(ProgressionE2ETestSuite, ColorPickerFromIdle) {
    e2eColorPickerFromIdle(this);
}

TEST_F(ProgressionE2ETestSuite, EasyModeLoss) {
    e2eEasyModeLoss(this);
}

TEST_F(ProgressionE2ETestSuite, HardModeLoss) {
    e2eHardModeLoss(this);
}

TEST_F(ProgressionE2ETestSuite, FirewallDecryptProgression) {
    e2eFirewallDecryptProgression(this);
}

TEST_F(ProgressionE2ETestSuite, FirewallDecryptHard) {
    e2eFirewallDecryptHard(this);
}

TEST_F(ProgressionE2ETestSuite, NvsRoundTrip) {
    e2eNvsRoundTrip(this);
}

TEST_F(ProgressionE2ETestSuite, PromptAutoDismiss) {
    e2ePromptAutoDismiss(this);
}

TEST_F(ProgressionE2ETestSuite, DifficultyGatingDynamic) {
    e2eDifficultyGatingDynamic(this);
}

TEST_F(ProgressionE2ETestSuite, EquipLaterViaPicker) {
    e2eEquipLaterViaPicker(this);
}

// ============================================
// RE-ENCOUNTER TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, FirstEncounterLaunchesEasy) {
    firstEncounterLaunchesEasy(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterWithButtonShowsPrompt) {
    reencounterWithButtonShowsPrompt(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterChooseHardLaunchesHard) {
    reencounterChooseHardLaunchesHard(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterChooseEasyLaunchesRecreational) {
    reencounterChooseEasyLaunchesRecreational(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterChooseSkipReturnsToIdle) {
    reencounterChooseSkipReturnsToIdle(this);
}

TEST_F(NegativeFlowTestSuite, FullyCompletedReencounterAllRecreational) {
    fullyCompletedReencounterAllRecreational(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterTimeoutDefaultsToSkip) {
    reencounterTimeoutDefaultsToSkip(this);
}

TEST_F(NegativeFlowTestSuite, RecreationalWinSkipsRewards) {
    recreationalWinSkipsRewards(this);
}

// ============================================
// COLOR PROMPT CONTEXT TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, ColorPromptFirstProfileShowsEquip) {
    colorPromptFirstProfileShowsEquip(this);
}

TEST_F(NegativeFlowTestSuite, ColorPromptWithExistingShowsSwap) {
    colorPromptWithExistingShowsSwap(this);
}

// ============================================
// COLOR PICKER BUTTON SWAP TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, PickerUpButtonEquipsProfile) {
    pickerUpButtonEquipsProfile(this);
}

TEST_F(NegativeFlowTestSuite, PickerDownButtonCyclesProfile) {
    pickerDownButtonCyclesProfile(this);
}

TEST_F(NegativeFlowTestSuite, PickerShowsRoleAwareDefaultName) {
    pickerShowsRoleAwareDefaultName(this);
}

// ============================================
// IDLE PALETTE INDICATOR TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, IdleShowsPaletteIndicator) {
    idleShowsPaletteIndicator(this);
}

TEST_F(NegativeFlowTestSuite, IdleShowsEquippedPaletteName) {
    idleShowsEquippedPaletteName(this);
}

// ============================================
// BOUNTY ROLE TESTS
// ============================================

TEST_F(BountyFlowTestSuite, BountyFdnEasyWin) {
    bountyFdnEasyWin(this);
}

TEST_F(BountyFlowTestSuite, BountyFdnHardWinColorPrompt) {
    bountyFdnHardWinColorPrompt(this);
}

TEST_F(BountyFlowTestSuite, BountyReencounterPrompt) {
    bountyReencounterPrompt(this);
}
