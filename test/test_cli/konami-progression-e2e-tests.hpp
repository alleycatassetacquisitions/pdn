#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// KONAMI PROGRESSION E2E TEST FIXTURE
//
// End-to-end tests for Konami button progression:
// - Full 7-game progression
// - Partial progression
// - Duplicate win handling
// - Konami unlock check
// - Button mapping verification
// - Progression persistence
// ============================================

class KonamiProgressionE2ETestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(player_);

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    DeviceInstance player_;
};

// ============================================
// KONAMI PROGRESSION E2E TESTS
// ============================================

/*
 * Test: Full 7-game progression — Unlock all 7 buttons sequentially, verify all bits set.
 * Tests the progression system by directly unlocking all 7 Konami buttons and verifying
 * the konamiProgress bitmask accumulates correctly.
 */
void konamiProgressionFullSevenGames(KonamiProgressionE2ETestSuite* suite) {
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);
    ASSERT_FALSE(suite->player_.player->hasAllKonamiButtons());

    // Game mapping:
    // GameType 1 (GHOST_RUNNER)      -> KonamiButton 0 (UP)
    // GameType 2 (SPIKE_VECTOR)      -> KonamiButton 1 (DOWN)
    // GameType 3 (FIREWALL_DECRYPT)  -> KonamiButton 2 (LEFT)
    // GameType 4 (CIPHER_PATH)       -> KonamiButton 3 (RIGHT)
    // GameType 5 (EXPLOIT_SEQUENCER) -> KonamiButton 4 (B)
    // GameType 6 (BREACH_DEFENSE)    -> KonamiButton 5 (A)
    // GameType 7 (SIGNAL_ECHO)       -> KonamiButton 6 (START)

    // Unlock all 7 buttons sequentially and verify progress accumulates
    for (uint8_t i = 0; i < 7; i++) {
        suite->player_.player->unlockKonamiButton(i);
        ASSERT_TRUE(suite->player_.player->hasUnlockedButton(i));

        // Verify expected bit pattern after each unlock
        uint8_t expectedProgress = (1 << (i + 1)) - 1;  // All bits up to i set
        ASSERT_EQ(suite->player_.player->getKonamiProgress(), expectedProgress);
    }

    // After all 7 games, all buttons should be unlocked
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0x7F);  // All 7 bits set
    ASSERT_TRUE(suite->player_.player->hasAllKonamiButtons());
}

/*
 * Test: Partial progression — Unlock 3 buttons, verify only 3 buttons set.
 */
void konamiProgressionPartialThreeGames(KonamiProgressionE2ETestSuite* suite) {
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);

    // Unlock GHOST_RUNNER (button 0)
    suite->player_.player->unlockKonamiButton(0);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(0));

    // Unlock SPIKE_VECTOR (button 1)
    suite->player_.player->unlockKonamiButton(1);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(1));

    // Unlock CIPHER_PATH (button 3)
    suite->player_.player->unlockKonamiButton(3);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(3));

    // Verify exactly 3 buttons are set: 0, 1, 3
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(progress, 0b00001011);  // bits 0, 1, 3 set
    ASSERT_FALSE(suite->player_.player->hasAllKonamiButtons());

    // Verify other buttons are NOT set
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(2));  // LEFT
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(4));  // B
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(5));  // A
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(6));  // START
}

/*
 * Test: Duplicate win — Unlock same button twice, verify button only set once.
 */
void konamiProgressionDuplicateWin(KonamiProgressionE2ETestSuite* suite) {
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);

    // Unlock GHOST_RUNNER (button 0) first time
    suite->player_.player->unlockKonamiButton(0);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(0));
    uint8_t progressAfterFirst = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(progressAfterFirst, 0b00000001);  // bit 0 set

    // Unlock GHOST_RUNNER (button 0) second time
    suite->player_.player->unlockKonamiButton(0);
    uint8_t progressAfterSecond = suite->player_.player->getKonamiProgress();

    // Progress should be unchanged
    ASSERT_EQ(progressAfterSecond, progressAfterFirst);
    ASSERT_EQ(progressAfterSecond, 0b00000001);  // Still only bit 0 set
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(0));
}

/*
 * Test: Konami unlock check — After all 7 buttons, verify hasAllKonamiButtons() == true.
 */
void konamiProgressionUnlockCheck(KonamiProgressionE2ETestSuite* suite) {
    // Unlock buttons 0-6 directly
    for (uint8_t i = 0; i < 7; i++) {
        suite->player_.player->unlockKonamiButton(i);
    }

    // Verify all buttons unlocked
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0x7F);
    ASSERT_TRUE(suite->player_.player->hasAllKonamiButtons());
    ASSERT_TRUE(suite->player_.player->isKonamiComplete());

    // Verify each button individually
    for (uint8_t i = 0; i < 7; i++) {
        ASSERT_TRUE(suite->player_.player->hasUnlockedButton(i));
    }
}

/*
 * Test: Button mapping verification — Each game maps to correct button via getRewardForGame().
 */
void konamiProgressionButtonMapping(KonamiProgressionE2ETestSuite* suite) {
    // Verify getRewardForGame mapping
    ASSERT_EQ(static_cast<uint8_t>(getRewardForGame(GameType::GHOST_RUNNER)), 0);
    ASSERT_EQ(static_cast<uint8_t>(getRewardForGame(GameType::SPIKE_VECTOR)), 1);
    ASSERT_EQ(static_cast<uint8_t>(getRewardForGame(GameType::FIREWALL_DECRYPT)), 2);
    ASSERT_EQ(static_cast<uint8_t>(getRewardForGame(GameType::CIPHER_PATH)), 3);
    ASSERT_EQ(static_cast<uint8_t>(getRewardForGame(GameType::EXPLOIT_SEQUENCER)), 4);
    ASSERT_EQ(static_cast<uint8_t>(getRewardForGame(GameType::BREACH_DEFENSE)), 5);
    ASSERT_EQ(static_cast<uint8_t>(getRewardForGame(GameType::SIGNAL_ECHO)), 6);

    // Verify button name mapping
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::UP), "UP");
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::DOWN), "DOWN");
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::LEFT), "LEFT");
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::RIGHT), "RIGHT");
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::B), "B");
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::A), "A");
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::START), "START");
}

/*
 * Test: Progression persistence — Player retains buttons across multiple unlocks.
 */
void konamiProgressionPersistence(KonamiProgressionE2ETestSuite* suite) {
    // Unlock first button (GHOST_RUNNER)
    suite->player_.player->unlockKonamiButton(0);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(0));
    uint8_t progressAfterFirst = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(progressAfterFirst, 0b00000001);

    // Unlock second button (SPIKE_VECTOR)
    suite->player_.player->unlockKonamiButton(1);

    // Verify both buttons are still set
    uint8_t progressAfterSecond = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(progressAfterSecond, 0b00000011);  // bits 0 and 1 set
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(0));  // First button persisted
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(1));  // Second button added

    // Unlock third button (CIPHER_PATH)
    suite->player_.player->unlockKonamiButton(3);

    // Verify all three buttons are still set
    uint8_t progressAfterThird = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(progressAfterThird, 0b00001011);  // bits 0, 1, 3 set
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(0));  // Still persisted
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(1));  // Still persisted
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(3));  // Newly added
}

#endif // NATIVE_BUILD
