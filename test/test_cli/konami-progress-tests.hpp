//
// Konami Progress Tests — Player bitmask operations and serialization
//

#pragma once

#include <gtest/gtest.h>
#include "game/player.hpp"
#include "device/device-types.hpp"
#include <ArduinoJson.h>

// ============================================
// KONAMI PROGRESS TEST SUITE
// ============================================

class KonamiProgressTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = new Player("0010", Allegiance::RESISTANCE, true);
    }

    void TearDown() override {
        delete player_;
    }

    Player* player_;
};

// Test: Unlocking a single button sets the correct bit
void konamiUnlockSingleButton(KonamiProgressTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::START);

    // START = bit 6 = 0b01000000 = 64
    ASSERT_EQ(suite->player_->getKonamiProgress(), 64);
    ASSERT_TRUE(suite->player_->hasUnlockedButton(KonamiButton::START));
}

// Test: Unlocking multiple buttons produces correct composite bitmask
void konamiUnlockMultipleButtons(KonamiProgressTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::UP);     // bit 0
    suite->player_->unlockKonamiButton(KonamiButton::DOWN);   // bit 1
    suite->player_->unlockKonamiButton(KonamiButton::START);  // bit 6

    // 0b01000011 = 67
    ASSERT_EQ(suite->player_->getKonamiProgress(), 67);
    ASSERT_EQ(suite->player_->getUnlockedButtonCount(), 3);
}

// Test: hasUnlockedButton returns true for unlocked buttons
void konamiHasUnlockedButtonTrue(KonamiProgressTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::LEFT);   // bit 2
    suite->player_->unlockKonamiButton(KonamiButton::A);      // bit 5

    ASSERT_TRUE(suite->player_->hasUnlockedButton(KonamiButton::LEFT));
    ASSERT_TRUE(suite->player_->hasUnlockedButton(KonamiButton::A));
}

// Test: hasUnlockedButton returns false for locked buttons
void konamiHasUnlockedButtonFalse(KonamiProgressTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::UP);

    ASSERT_FALSE(suite->player_->hasUnlockedButton(KonamiButton::DOWN));
    ASSERT_FALSE(suite->player_->hasUnlockedButton(KonamiButton::START));
    ASSERT_FALSE(suite->player_->hasUnlockedButton(KonamiButton::B));
}

// Test: Unlocking all 7 buttons triggers hasAllKonamiButtons
void konamiAllButtonsUnlocked(KonamiProgressTestSuite* suite) {
    ASSERT_FALSE(suite->player_->hasAllKonamiButtons());

    suite->player_->unlockKonamiButton(KonamiButton::UP);
    suite->player_->unlockKonamiButton(KonamiButton::DOWN);
    suite->player_->unlockKonamiButton(KonamiButton::LEFT);
    suite->player_->unlockKonamiButton(KonamiButton::RIGHT);
    suite->player_->unlockKonamiButton(KonamiButton::B);
    suite->player_->unlockKonamiButton(KonamiButton::A);
    suite->player_->unlockKonamiButton(KonamiButton::START);

    ASSERT_TRUE(suite->player_->hasAllKonamiButtons());
    ASSERT_EQ(suite->player_->getKonamiProgress(), KONAMI_ALL_UNLOCKED);
    ASSERT_EQ(suite->player_->getUnlockedButtonCount(), 7);
}

// Test: Unlocking the same button twice is idempotent
void konamiDuplicateUnlockIdempotent(KonamiProgressTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::START);
    uint8_t afterFirst = suite->player_->getKonamiProgress();

    suite->player_->unlockKonamiButton(KonamiButton::START);
    uint8_t afterSecond = suite->player_->getKonamiProgress();

    ASSERT_EQ(afterFirst, afterSecond);
    ASSERT_EQ(suite->player_->getUnlockedButtonCount(), 1);
}

// Test: toJson includes konami_progress field
void konamiProgressSerializesToJson(KonamiProgressTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::UP);    // bit 0
    suite->player_->unlockKonamiButton(KonamiButton::LEFT);  // bit 2

    std::string json = suite->player_->toJson();

    // Parse and verify
    JsonDocument doc;
    deserializeJson(doc, json);
    ASSERT_TRUE(doc.containsKey("konami_progress"));
    ASSERT_EQ(doc["konami_progress"].as<uint8_t>(), 5);  // 0b00000101

    // Color profile defaults
    ASSERT_TRUE(doc.containsKey("equipped_color_profile"));
    ASSERT_EQ(doc["equipped_color_profile"].as<uint8_t>(), 0);  // QUICKDRAW
}

// Test: fromJson restores konami_progress correctly
void konamiProgressDeserializesFromJson(KonamiProgressTestSuite* suite) {
    // Serialize a player with progress
    suite->player_->unlockKonamiButton(KonamiButton::UP);
    suite->player_->unlockKonamiButton(KonamiButton::START);
    suite->player_->setEquippedColorProfile(GameType::SIGNAL_ECHO);
    suite->player_->addColorProfileEligibility(GameType::SIGNAL_ECHO);

    std::string json = suite->player_->toJson();

    // Deserialize into a fresh player
    Player restored;
    restored.fromJson(json);

    ASSERT_EQ(restored.getKonamiProgress(), suite->player_->getKonamiProgress());
    ASSERT_TRUE(restored.hasUnlockedButton(KonamiButton::UP));
    ASSERT_TRUE(restored.hasUnlockedButton(KonamiButton::START));
    ASSERT_FALSE(restored.hasUnlockedButton(KonamiButton::DOWN));
    ASSERT_EQ(restored.getEquippedColorProfile(), GameType::SIGNAL_ECHO);
    ASSERT_TRUE(restored.hasColorProfileEligibility(GameType::SIGNAL_ECHO));
}

// Test: fromJson with missing konami_progress defaults to 0 (backward compat)
void konamiProgressDeserializeMissingField(KonamiProgressTestSuite* suite) {
    // JSON without konami_progress (old server format)
    std::string oldJson = R"({"id":"0010","name":"Test","allegiance":"none","faction":"Guild","hunter":true})";

    Player restored;
    restored.fromJson(oldJson);

    ASSERT_EQ(restored.getKonamiProgress(), 0);
    ASSERT_EQ(restored.getEquippedColorProfile(), GameType::QUICKDRAW);
    ASSERT_FALSE(restored.hasAllKonamiButtons());
    ASSERT_EQ(restored.getUnlockedButtonCount(), 0);
}
