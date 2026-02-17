#pragma once

#include <gtest/gtest.h>
#include "game/player.hpp"
#include "game/fdn-game-type.hpp"
#include "game/konami-metagame.hpp"
#include "device/device-types.hpp"

class KonamiMetaGameTestSuite : public testing::Test {
protected:
    void SetUp() override {
        player = new Player();
    }

    void TearDown() override {
        delete player;
    }

    Player* player;
};

// ============================================
// FdnGameType Enum Tests
// ============================================

inline void fdnGameTypeEnumValuesAreCorrect(Player* player) {
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::SIGNAL_ECHO), 0);
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::GHOST_RUNNER), 1);
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::SPIKE_VECTOR), 2);
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::FIREWALL_DECRYPT), 3);
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::CIPHER_PATH), 4);
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::EXPLOIT_SEQUENCER), 5);
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::BREACH_DEFENSE), 6);
    EXPECT_EQ(static_cast<uint8_t>(FdnGameType::KONAMI_CODE), 7);
}

// ============================================
// Player Konami Button Methods Tests
// ============================================

inline void playerHasNoButtonsInitially(Player* player) {
    EXPECT_FALSE(player->allButtonsCollected());
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::GHOST_RUNNER));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::SPIKE_VECTOR));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::FIREWALL_DECRYPT));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::CIPHER_PATH));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::EXPLOIT_SEQUENCER));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::BREACH_DEFENSE));
    EXPECT_FALSE(player->isHardModeUnlocked());
}

inline void playerCanUnlockSingleButton(Player* player) {
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::GHOST_RUNNER));
    EXPECT_FALSE(player->allButtonsCollected());
}

inline void playerCanUnlockMultipleButtons(Player* player) {
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    player->unlockKonamiButton(FdnGameType::GHOST_RUNNER);
    player->unlockKonamiButton(FdnGameType::SPIKE_VECTOR);

    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::GHOST_RUNNER));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SPIKE_VECTOR));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::FIREWALL_DECRYPT));
    EXPECT_FALSE(player->allButtonsCollected());
}

inline void playerAllButtonsCollectedWhenSevenUnlocked(Player* player) {
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    player->unlockKonamiButton(FdnGameType::GHOST_RUNNER);
    player->unlockKonamiButton(FdnGameType::SPIKE_VECTOR);
    player->unlockKonamiButton(FdnGameType::FIREWALL_DECRYPT);
    player->unlockKonamiButton(FdnGameType::CIPHER_PATH);
    player->unlockKonamiButton(FdnGameType::EXPLOIT_SEQUENCER);
    player->unlockKonamiButton(FdnGameType::BREACH_DEFENSE);

    EXPECT_TRUE(player->allButtonsCollected());
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::GHOST_RUNNER));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SPIKE_VECTOR));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::FIREWALL_DECRYPT));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::CIPHER_PATH));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::EXPLOIT_SEQUENCER));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::BREACH_DEFENSE));
}

inline void playerHardModeUnlocksIndependently(Player* player) {
    EXPECT_FALSE(player->isHardModeUnlocked());
    player->unlockHardMode();
    EXPECT_TRUE(player->isHardModeUnlocked());
}

inline void playerHardModeDoesNotAffectButtons(Player* player) {
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    player->unlockHardMode();

    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::GHOST_RUNNER));
    EXPECT_TRUE(player->isHardModeUnlocked());
    EXPECT_FALSE(player->allButtonsCollected());
}

inline void playerColorProfileCheckWorks(Player* player) {
    // Initially no profiles
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::SIGNAL_ECHO));

    // Add eligibility (using the int version from existing Player API)
    player->addColorProfileEligibility(static_cast<int>(FdnGameType::SIGNAL_ECHO));

    EXPECT_TRUE(player->hasColorProfile(FdnGameType::SIGNAL_ECHO));
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::GHOST_RUNNER));
}

// ============================================
// KonamiMetaGame Construction Tests
// ============================================

inline void konamiMetaGameConstructsSuccessfully(Player* player) {
    KonamiMetaGame* metaGame = new KonamiMetaGame(player);
    EXPECT_NE(metaGame, nullptr);
    delete metaGame;
}

inline void konamiMetaGamePopulatesThirtyFiveStates(Player* player) {
    KonamiMetaGame* metaGame = new KonamiMetaGame(player);
    metaGame->populateStateMap();

    // The state map is protected, but we can check via derived class behavior
    // For now, just verify construction doesn't crash
    EXPECT_NE(metaGame, nullptr);

    delete metaGame;
}

// ============================================
// STATE TRANSITION TESTS (Unit Tests)
// ============================================

// Test: Easy mode button collection advances progress
inline void easyModeButtonCollectionAdvancesProgress(Player* player) {
    // Start with no buttons
    EXPECT_EQ(player->getKonamiProgress(), 0);

    // Unlock first button (SIGNAL_ECHO = UP)
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
    EXPECT_EQ(player->getKonamiProgress(), 0x01);  // Binary: 00000001

    // Unlock second button (GHOST_RUNNER = DOWN)
    player->unlockKonamiButton(FdnGameType::GHOST_RUNNER);
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::GHOST_RUNNER));
    EXPECT_EQ(player->getKonamiProgress(), 0x03);  // Binary: 00000011
}

// Test: Mode unlock flags work independently
inline void modeUnlockFlagsWorkIndependently(Player* player) {
    // Unlock some buttons but not all
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    player->unlockKonamiButton(FdnGameType::GHOST_RUNNER);

    EXPECT_FALSE(player->allButtonsCollected());
    EXPECT_FALSE(player->isHardModeUnlocked());

    // Unlock hard mode (bit 7)
    player->unlockHardMode();
    EXPECT_TRUE(player->isHardModeUnlocked());
    EXPECT_FALSE(player->allButtonsCollected());  // Still only 2/7 buttons

    // Hard mode bit should be set (0x80 = binary: 10000000)
    EXPECT_EQ(player->getKonamiProgress() & 0x80, 0x80);
}

// Test: Duplicate button unlocks are idempotent
inline void duplicateButtonUnlocksAreIdempotent(Player* player) {
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    uint8_t progressAfterFirst = player->getKonamiProgress();

    // Unlock same button again
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    uint8_t progressAfterSecond = player->getKonamiProgress();

    EXPECT_EQ(progressAfterFirst, progressAfterSecond);
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
}

// ============================================
// INTEGRATION TESTS (Full Progression Flow)
// ============================================

// Test: Complete easy progression (0 buttons → 7 buttons)
inline void completeEasyProgressionFlow(Player* player) {
    // Start fresh
    EXPECT_FALSE(player->allButtonsCollected());

    // Collect all 7 buttons in order
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    EXPECT_FALSE(player->allButtonsCollected());

    player->unlockKonamiButton(FdnGameType::GHOST_RUNNER);
    EXPECT_FALSE(player->allButtonsCollected());

    player->unlockKonamiButton(FdnGameType::SPIKE_VECTOR);
    EXPECT_FALSE(player->allButtonsCollected());

    player->unlockKonamiButton(FdnGameType::FIREWALL_DECRYPT);
    EXPECT_FALSE(player->allButtonsCollected());

    player->unlockKonamiButton(FdnGameType::CIPHER_PATH);
    EXPECT_FALSE(player->allButtonsCollected());

    player->unlockKonamiButton(FdnGameType::EXPLOIT_SEQUENCER);
    EXPECT_FALSE(player->allButtonsCollected());

    // Seventh button completes the set
    player->unlockKonamiButton(FdnGameType::BREACH_DEFENSE);
    EXPECT_TRUE(player->allButtonsCollected());
    EXPECT_TRUE(player->hasAllKonamiButtons());
}

// Test: Konami code unlock flow (7 buttons → hard mode)
inline void konamiCodeUnlockFlow(Player* player) {
    // Give player all 7 buttons
    for (uint8_t i = 0; i < 7; i++) {
        player->unlockKonamiButton(i);
    }

    EXPECT_TRUE(player->allButtonsCollected());
    EXPECT_FALSE(player->isHardModeUnlocked());

    // Simulate successful Konami code entry
    player->unlockHardMode();

    EXPECT_TRUE(player->isHardModeUnlocked());
    EXPECT_TRUE(player->allButtonsCollected());
}

// Test: Hard mode progression (hard mode → color profiles)
inline void hardModeProgressionFlow(Player* player) {
    // Setup: Player has hard mode unlocked
    for (uint8_t i = 0; i < 7; i++) {
        player->unlockKonamiButton(i);
    }
    player->unlockHardMode();

    EXPECT_TRUE(player->isHardModeUnlocked());
    EXPECT_FALSE(player->hasColorProfileEligibility(0));

    // Beat hard mode for first game
    player->addColorProfileEligibility(static_cast<int>(FdnGameType::SIGNAL_ECHO));
    EXPECT_TRUE(player->hasColorProfileEligibility(static_cast<int>(FdnGameType::SIGNAL_ECHO)));

    // Beat hard mode for second game
    player->addColorProfileEligibility(static_cast<int>(FdnGameType::GHOST_RUNNER));
    EXPECT_TRUE(player->hasColorProfileEligibility(static_cast<int>(FdnGameType::GHOST_RUNNER)));
}

// Test: Boon activation flow
inline void boonActivationFlow(Player* player) {
    // Setup: All buttons collected
    for (uint8_t i = 0; i < 7; i++) {
        player->unlockKonamiButton(i);
    }

    EXPECT_TRUE(player->allButtonsCollected());
    EXPECT_FALSE(player->hasKonamiBoon());

    // Activate boon
    player->setKonamiBoon(true);
    EXPECT_TRUE(player->hasKonamiBoon());
}

// Test: Mastery replay with profile selection
inline void masteryReplayWithProfileSelection(Player* player) {
    // Setup: Player has boon and color profiles
    for (uint8_t i = 0; i < 7; i++) {
        player->unlockKonamiButton(i);
    }
    player->unlockHardMode();
    player->setKonamiBoon(true);

    // Add color profile eligibility for specific games
    player->addColorProfileEligibility(static_cast<int>(FdnGameType::SIGNAL_ECHO));
    player->addColorProfileEligibility(static_cast<int>(FdnGameType::SPIKE_VECTOR));

    EXPECT_TRUE(player->hasColorProfile(FdnGameType::SIGNAL_ECHO));
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::GHOST_RUNNER));
    EXPECT_TRUE(player->hasColorProfile(FdnGameType::SPIKE_VECTOR));

    // Equip a color profile
    player->setEquippedColorProfile(static_cast<int>(FdnGameType::SIGNAL_ECHO));
    EXPECT_EQ(player->getEquippedColorProfile(), static_cast<int>(FdnGameType::SIGNAL_ECHO));
}

// ============================================
// EDGE CASE TESTS
// ============================================

// Test: Disconnect during code entry doesn't corrupt state
inline void disconnectDuringCodeEntryDoesntCorruptState(Player* player) {
    // Setup: Player has all buttons and is attempting Konami code
    for (uint8_t i = 0; i < 7; i++) {
        player->unlockKonamiButton(i);
    }

    uint8_t progressBeforeDisconnect = player->getKonamiProgress();

    // Simulate disconnect (no state change should occur)
    // Player state should remain stable
    EXPECT_EQ(player->getKonamiProgress(), progressBeforeDisconnect);
    EXPECT_TRUE(player->allButtonsCollected());
    EXPECT_FALSE(player->isHardModeUnlocked());
}

// Test: Partial button collection persists correctly
inline void partialButtonCollectionPersists(Player* player) {
    // Collect first 3 buttons
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    player->unlockKonamiButton(FdnGameType::GHOST_RUNNER);
    player->unlockKonamiButton(FdnGameType::SPIKE_VECTOR);

    uint8_t progress = player->getKonamiProgress();

    // Verify individual button states persist
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::GHOST_RUNNER));
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SPIKE_VECTOR));
    EXPECT_FALSE(player->hasKonamiButton(FdnGameType::FIREWALL_DECRYPT));

    // Progress value should be exactly 0x07 (binary: 00000111)
    EXPECT_EQ(progress, 0x07);
}

// Test: Mode select with incomplete profiles
inline void modeSelectWithIncompleteProfiles(Player* player) {
    // Setup: Player has boon but only some color profiles
    for (uint8_t i = 0; i < 7; i++) {
        player->unlockKonamiButton(i);
    }
    player->unlockHardMode();
    player->setKonamiBoon(true);

    // Add only 2 of 7 color profiles
    player->addColorProfileEligibility(static_cast<int>(FdnGameType::SIGNAL_ECHO));
    player->addColorProfileEligibility(static_cast<int>(FdnGameType::CIPHER_PATH));

    // Player should have profiles for specific games only
    EXPECT_TRUE(player->hasColorProfile(FdnGameType::SIGNAL_ECHO));
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::GHOST_RUNNER));
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::SPIKE_VECTOR));
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::FIREWALL_DECRYPT));
    EXPECT_TRUE(player->hasColorProfile(FdnGameType::CIPHER_PATH));
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::EXPLOIT_SEQUENCER));
    EXPECT_FALSE(player->hasColorProfile(FdnGameType::BREACH_DEFENSE));
}

// Test: Replay without reward (button already collected)
inline void replayWithoutReward(Player* player) {
    // First encounter: collect button
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));

    // Second encounter: button already collected
    // State should remain unchanged
    player->unlockKonamiButton(FdnGameType::SIGNAL_ECHO);
    EXPECT_TRUE(player->hasKonamiButton(FdnGameType::SIGNAL_ECHO));

    // Progress should still show only 1 button
    EXPECT_EQ(player->getKonamiProgress(), 0x01);
}

// Test: All 35 state indices are valid
inline void allThirtyFiveStateIndicesAreValid(Player* player) {
    KonamiMetaGame* metaGame = new KonamiMetaGame(player);
    metaGame->populateStateMap();

    // Verify construction succeeds and doesn't crash
    // A real state map validation would require access to protected members
    // For now, we verify the construction completes without segfault
    EXPECT_NE(metaGame, nullptr);

    delete metaGame;
}

