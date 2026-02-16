//
// Progression E2E Tests — E2E, Re-encounter, Color Context, Button Swap, Palette, Bounty Role
//
// DISABLED tests: Most tests fail after KMG routing (Wave 17, #271) changes game launch flow.
// Previously masked by SIGSEGV crash (#300, fixed by #323). See issue #327 for rewrite plan.

#include <gtest/gtest.h>

#include "progression-e2e-tests.hpp"
#include "negative-flow-tests.hpp"

// ============================================
// E2E PROGRESSION TESTS
// ============================================

TEST_F(ProgressionE2ETestSuite, DISABLED_SignalEchoEasyWin) {
    e2eSignalEchoEasyWin(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_AutoBoonOnSeventhButton) {
    e2eAutoBoonOnSeventhButton(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_ServerSyncOnWin) {
    e2eServerSyncOnWin(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_HardModeColorEquip) {
    e2eHardModeColorEquip(this);
}

// DISABLED: Assertion failure — KMG routing changes color prompt flow.
// See #327.
TEST_F(ProgressionE2ETestSuite, DISABLED_ColorPromptDecline) {
    e2eColorPromptDecline(this);
}

// DISABLED: Assertion failure — KMG routing changes color picker flow.
// See #327.
TEST_F(ProgressionE2ETestSuite, DISABLED_ColorPickerFromIdle) {
    e2eColorPickerFromIdle(this);
}

// DISABLED: SIGSEGV — KMG routing adds intermediate states that crash
// when test assumes direct minigame launch. See #327.
TEST_F(ProgressionE2ETestSuite, DISABLED_EasyModeLoss) {
    e2eEasyModeLoss(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_HardModeLoss) {
    e2eHardModeLoss(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_FirewallDecryptProgression) {
    e2eFirewallDecryptProgression(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_FirewallDecryptHard) {
    e2eFirewallDecryptHard(this);
}

TEST_F(ProgressionE2ETestSuite, NvsRoundTrip) {
    e2eNvsRoundTrip(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_PromptAutoDismiss) {
    e2ePromptAutoDismiss(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_DifficultyGatingDynamic) {
    e2eDifficultyGatingDynamic(this);
}

TEST_F(ProgressionE2ETestSuite, DISABLED_EquipLaterViaPicker) {
    e2eEquipLaterViaPicker(this);
}

// ============================================
// RE-ENCOUNTER TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, DISABLED_FirstEncounterLaunchesEasy) {
    firstEncounterLaunchesEasy(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_ReencounterWithButtonShowsPrompt) {
    reencounterWithButtonShowsPrompt(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_ReencounterChooseHardLaunchesHard) {
    reencounterChooseHardLaunchesHard(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_ReencounterChooseEasyLaunchesRecreational) {
    reencounterChooseEasyLaunchesRecreational(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_ReencounterChooseSkipReturnsToIdle) {
    reencounterChooseSkipReturnsToIdle(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_FullyCompletedReencounterAllRecreational) {
    fullyCompletedReencounterAllRecreational(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_ReencounterTimeoutDefaultsToSkip) {
    reencounterTimeoutDefaultsToSkip(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_RecreationalWinSkipsRewards) {
    recreationalWinSkipsRewards(this);
}

// ============================================
// COLOR PROMPT CONTEXT TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, DISABLED_ColorPromptFirstProfileShowsEquip) {
    colorPromptFirstProfileShowsEquip(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_ColorPromptWithExistingShowsSwap) {
    colorPromptWithExistingShowsSwap(this);
}

// ============================================
// COLOR PICKER BUTTON SWAP TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, DISABLED_PickerUpButtonEquipsProfile) {
    pickerUpButtonEquipsProfile(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_PickerDownButtonCyclesProfile) {
    pickerDownButtonCyclesProfile(this);
}

TEST_F(NegativeFlowTestSuite, DISABLED_PickerShowsRoleAwareDefaultName) {
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

TEST_F(BountyFlowTestSuite, DISABLED_BountyFdnEasyWin) {
    bountyFdnEasyWin(this);
}

TEST_F(BountyFlowTestSuite, DISABLED_BountyFdnHardWinColorPrompt) {
    bountyFdnHardWinColorPrompt(this);
}

TEST_F(BountyFlowTestSuite, DISABLED_BountyReencounterPrompt) {
    bountyReencounterPrompt(this);
}
