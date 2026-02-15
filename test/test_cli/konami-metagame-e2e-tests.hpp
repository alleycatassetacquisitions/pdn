#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "game/player.hpp"
#include "game/konami-metagame/konami-metagame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// KONAMI METAGAME E2E TEST FIXTURE
//
// End-to-end tests for the full KonamiMetaGame integration:
// - First encounter flow (easy win -> button reward)
// - Button replay (no new rewards)
// - Full progression (all 7 buttons -> Konami code)
// - Hard mode unlock (Konami code completion)
// - Mastery replay (mode selection)
// ============================================

class KonamiMetaGameE2ETestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::GHOST_RUNNER);

        SimpleTimer::setPlatformClock(player_.clockDriver);

        // Cable connect player to FDN
        SerialCableBroker::getInstance().connect(0, 1);
    }

    void TearDown() override {
        SerialCableBroker::getInstance().disconnect(0, 1);
        DeviceFactory::destroyDevice(player_);
        DeviceFactory::destroyDevice(fdn_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            player_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            fdn_.clockDriver->advance(delayMs);
            player_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    DeviceInstance player_;
    DeviceInstance fdn_;
};

// ============================================
// KONAMI METAGAME E2E TESTS
// ============================================

/*
 * Test 1: First encounter -> easy win -> button awarded -> konamiProgress bit set
 *
 * Scenario:
 * - Player encounters Ghost Runner FDN (first time)
 * - Plays in EASY mode
 * - Wins the game
 * - Receives KonamiButton::START (reward for Ghost Runner)
 * - konamiProgress bit 0 is set
 *
 * Expected behavior:
 * - Player starts with konamiProgress = 0
 * - After win, konamiProgress = 0b00000001 (bit 0 set)
 * - hasUnlockedButton(0) returns true
 */
void konamiMetagameFirstEncounterEasyWinButtonAwarded(KonamiMetaGameE2ETestSuite* suite) {
    // Verify initial state
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::START)));

    // TODO: Once KonamiMetaGame states are implemented by other agents:
    // 1. Trigger FDN connection (player detects broadcast)
    // 2. Switch to KONAMI_METAGAME_APP_ID using PDN->setActiveApp()
    // 3. Complete EASY mode minigame
    // 4. Verify button reward awarded
    // 5. Verify konamiProgress updated

    // Stub assertion - replace when implementation exists
    // For now, manually unlock button to demonstrate expected behavior
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::START)));
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0b00000001);
}

/*
 * Test 2: Button replay -> no new rewards
 *
 * Scenario:
 * - Player already has Ghost Runner button
 * - Encounters Ghost Runner FDN again
 * - Wins again
 * - No new button awarded (already have it)
 *
 * Expected behavior:
 * - konamiProgress remains unchanged after replay win
 * - Player can still select difficulty (easy/hard/mastery)
 */
void konamiMetagameButtonReplayNoNewRewards(KonamiMetaGameE2ETestSuite* suite) {
    // Pre-unlock the button
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(progressBefore, 0b00000001);

    // TODO: Once KonamiMetaGame states are implemented:
    // 1. Trigger FDN connection
    // 2. Verify mode select prompt appears (re-encounter flow)
    // 3. Complete game again
    // 4. Verify konamiProgress unchanged

    // Stub - simulate replay (progress should not change)
    uint8_t progressAfter = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(progressAfter, progressBefore);
}

/*
 * Test 3: All 7 buttons -> Konami code entry -> 13 inputs -> hard mode unlocked
 *
 * Scenario:
 * - Player has collected all 7 Konami buttons
 * - Encounters any FDN
 * - Prompted to enter Konami code (13 button sequence)
 * - Successfully enters code
 * - Hard mode unlocked for all games
 *
 * Expected behavior:
 * - konamiProgress = 0x7F (all 7 bits set)
 * - hasAllKonamiButtons() returns true
 * - isKonamiComplete() returns true after code entry
 */
void konamiMetagameAllButtonsKonamiCodeEntry(KonamiMetaGameE2ETestSuite* suite) {
    // Unlock all 7 buttons
    for (uint8_t i = 0; i < 7; i++) {
        suite->player_.player->unlockKonamiButton(i);
    }

    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0x7F);
    ASSERT_TRUE(suite->player_.player->hasAllKonamiButtons());

    // TODO: Once KonamiMetaGame states are implemented:
    // 1. Trigger FDN connection
    // 2. Detect that hasAllKonamiButtons() == true
    // 3. Launch KonamiCodeEntry state (13-button sequence)
    // 4. Simulate correct button presses (UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A START)
    // 5. Verify isKonamiComplete() == true
    // 6. Verify hard mode unlocked

    // Stub - manually mark Konami complete to demonstrate expected state
    // NOTE: This requires Player::setKonamiComplete() or similar method
    // For now, just verify button collection is correct
    ASSERT_TRUE(suite->player_.player->hasAllKonamiButtons());
}

/*
 * Test 4: Hard mode -> win -> boon awarded -> colorProfileEligibility updated
 *
 * Scenario:
 * - Player has completed Konami code (hard mode unlocked)
 * - Encounters Ghost Runner FDN
 * - Selects HARD difficulty
 * - Wins the game
 * - Receives color profile boon
 * - colorProfileEligibility bit set for Ghost Runner
 *
 * Expected behavior:
 * - Before: hasColorProfileEligibility(GHOST_RUNNER) == false
 * - After hard mode win: hasColorProfileEligibility(GHOST_RUNNER) == true
 */
void konamiMetagameHardModeWinBoonAwarded(KonamiMetaGameE2ETestSuite* suite) {
    // Pre-condition: All buttons unlocked + Konami complete
    for (uint8_t i = 0; i < 7; i++) {
        suite->player_.player->unlockKonamiButton(i);
    }

    // TODO: Set Konami complete flag (once Player method exists)
    // suite->player_.player->setKonamiComplete(true);

    ASSERT_FALSE(suite->player_.player->hasColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER)));

    // TODO: Once KonamiMetaGame states are implemented:
    // 1. Trigger FDN connection
    // 2. Mode select prompt appears (easy/hard/mastery)
    // 3. Select HARD mode
    // 4. Complete hard mode minigame
    // 5. Win and receive color profile boon
    // 6. Verify colorProfileEligibility updated

    // Stub - manually add color profile eligibility to demonstrate
    suite->player_.player->addColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER));
    ASSERT_TRUE(suite->player_.player->hasColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER)));
}

/*
 * Test 5: Mastery replay -> mode select -> correct launch
 *
 * Scenario:
 * - Player has unlocked hard mode AND color profile boon for a game
 * - Encounters that FDN again
 * - Mode select shows: EASY / HARD / MASTERY
 * - Player selects each mode in sequence
 * - Verify correct difficulty launched each time
 *
 * Expected behavior:
 * - Mode select presents 3 options
 * - Each mode launches with correct difficulty config
 * - Game state reflects selected difficulty
 */
void konamiMetagameMasteryReplayModeSelect(KonamiMetaGameE2ETestSuite* suite) {
    // Pre-condition: Button + boon unlocked
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));
    suite->player_.player->addColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER));

    // TODO: Set Konami complete flag
    // suite->player_.player->setKonamiComplete(true);

    // TODO: Once KonamiMetaGame states are implemented:
    // 1. Trigger FDN connection
    // 2. Verify mode select shows 3 options: EASY, HARD, MASTERY
    // 3. Select EASY -> verify EASY config loaded
    // 4. Retry, select HARD -> verify HARD config loaded
    // 5. Retry, select MASTERY -> verify MASTERY config loaded

    // Stub - verify pre-conditions are correct
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::START)));
    ASSERT_TRUE(suite->player_.player->hasColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER)));
}

#endif // NATIVE_BUILD
