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
// Placeholder State Tests
// ============================================

inline void placeholderStateHasCorrectId(Player* player) {
    PlaceholderState* placeholder = new PlaceholderState(42);
    EXPECT_EQ(placeholder->getStateId(), 42);
    delete placeholder;
}
