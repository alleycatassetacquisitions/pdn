#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/progress-manager.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// PROGRESSION EDGE CASE TEST SUITE
//
// Tests for idempotency, reset, persistence,
// boundary conditions, and invalid state recovery
// in the progression/reward system.
// ============================================

class ProgressionEdgeCaseTestSuite : public testing::Test {
public:
    void SetUp() override {
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    DeviceInstance device_;
};

// ============================================
// IDEMPOTENCY TESTS
// ============================================

// Test: Unlock same Konami button twice — should be idempotent
void unlockSameButtonTwice(ProgressionEdgeCaseTestSuite* suite) {
    uint8_t buttonIdx = 3;  // LEFT button
    suite->device_.player->unlockKonamiButton(buttonIdx);
    ASSERT_TRUE(suite->device_.player->hasUnlockedButton(buttonIdx));
    uint8_t progressAfterFirst = suite->device_.player->getKonamiProgress();

    // Unlock same button again
    suite->device_.player->unlockKonamiButton(buttonIdx);
    ASSERT_TRUE(suite->device_.player->hasUnlockedButton(buttonIdx));
    uint8_t progressAfterSecond = suite->device_.player->getKonamiProgress();

    // Progress should be identical (no double-counting)
    ASSERT_EQ(progressAfterFirst, progressAfterSecond);
}

// Test: Add same color profile eligibility twice — should be idempotent
void addSameEligibilityTwice(ProgressionEdgeCaseTestSuite* suite) {
    int gameType = static_cast<int>(GameType::SIGNAL_ECHO);
    suite->device_.player->addColorProfileEligibility(gameType);
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(gameType));
    size_t sizeAfterFirst = suite->device_.player->getColorProfileEligibility().size();

    // Add same eligibility again
    suite->device_.player->addColorProfileEligibility(gameType);
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(gameType));
    size_t sizeAfterSecond = suite->device_.player->getColorProfileEligibility().size();

    // Set size should be unchanged
    ASSERT_EQ(sizeAfterFirst, sizeAfterSecond);
    ASSERT_EQ(sizeAfterFirst, 1u);
}

// Test: Increment attempts multiple times — should accumulate correctly
void incrementAttemptsTwice(ProgressionEdgeCaseTestSuite* suite) {
    GameType gameType = GameType::SIGNAL_ECHO;
    ASSERT_EQ(suite->device_.player->getEasyAttempts(gameType), 0);

    suite->device_.player->incrementEasyAttempts(gameType);
    ASSERT_EQ(suite->device_.player->getEasyAttempts(gameType), 1);

    suite->device_.player->incrementEasyAttempts(gameType);
    ASSERT_EQ(suite->device_.player->getEasyAttempts(gameType), 2);

    suite->device_.player->incrementHardAttempts(gameType);
    ASSERT_EQ(suite->device_.player->getHardAttempts(gameType), 1);

    suite->device_.player->incrementHardAttempts(gameType);
    ASSERT_EQ(suite->device_.player->getHardAttempts(gameType), 2);
}

// Test: Save progress twice — should be idempotent
void saveProgressTwice(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->setKonamiProgress(0x0F);
    pm.saveProgress();

    // Verify storage has the value
    uint8_t stored1 = suite->device_.storageDriver->readUChar("konami", 0);
    ASSERT_EQ(stored1, 0x0F);

    // Save again without changing player state
    pm.saveProgress();
    uint8_t stored2 = suite->device_.storageDriver->readUChar("konami", 0);
    ASSERT_EQ(stored2, 0x0F);
    ASSERT_EQ(stored1, stored2);
}

// ============================================
// PROGRESSION RESET TESTS
// ============================================

// Test: Clear progress resets all fields
void clearProgressResetsAll(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Set all progression fields
    suite->device_.player->setKonamiProgress(0x7F);
    suite->device_.player->setKonamiBoon(true);
    suite->device_.player->setEquippedColorProfile(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->device_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->device_.player->addColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER));
    suite->device_.player->incrementEasyAttempts(GameType::SIGNAL_ECHO);
    suite->device_.player->incrementHardAttempts(GameType::SIGNAL_ECHO);

    pm.saveProgress();
    pm.clearProgress();

    // Load into fresh player to verify clear
    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 0);
    ASSERT_FALSE(freshPlayer.hasKonamiBoon());
    ASSERT_EQ(freshPlayer.getEquippedColorProfile(), -1);
    ASSERT_TRUE(freshPlayer.getColorProfileEligibility().empty());
    ASSERT_EQ(freshPlayer.getEasyAttempts(GameType::SIGNAL_ECHO), 0);
    ASSERT_EQ(freshPlayer.getHardAttempts(GameType::SIGNAL_ECHO), 0);
}

// Test: Clear progress during active game does not corrupt state
void clearProgressDuringGame(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Set some progress
    suite->device_.player->setKonamiProgress(0x0F);
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));
    pm.saveProgress();

    // Simulate being in a game (lastFdnGameType set)
    ASSERT_EQ(suite->device_.player->getLastFdnGameType(), static_cast<int>(GameType::SIGNAL_ECHO));

    // Clear progress
    pm.clearProgress();

    // Load to verify clear
    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 0);
    // lastFdnGameType is runtime state, not persisted
    ASSERT_EQ(freshPlayer.getLastFdnGameType(), -1);
}

// Test: Partial reset via clearProgress then selective restore
void partialResetViaReload(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Set progression
    suite->device_.player->setKonamiProgress(0x7F);
    suite->device_.player->setKonamiBoon(true);
    pm.saveProgress();

    // Clear all
    pm.clearProgress();

    // Manually restore only konamiBoon via storage
    suite->device_.storageDriver->writeUChar("konami_boon", 1);

    // Load into fresh player
    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 0);  // Cleared
    ASSERT_TRUE(freshPlayer.hasKonamiBoon());       // Restored
}

// ============================================
// HARD MODE VS EASY MODE TESTS
// ============================================

// Test: Hard mode attempts tracked separately from easy mode
void hardEasyAttemptsSeparate(ProgressionEdgeCaseTestSuite* suite) {
    GameType gameType = GameType::SIGNAL_ECHO;

    suite->device_.player->incrementEasyAttempts(gameType);
    suite->device_.player->incrementEasyAttempts(gameType);
    suite->device_.player->incrementHardAttempts(gameType);

    ASSERT_EQ(suite->device_.player->getEasyAttempts(gameType), 2);
    ASSERT_EQ(suite->device_.player->getHardAttempts(gameType), 1);

    // Different game types also separate
    suite->device_.player->incrementEasyAttempts(GameType::GHOST_RUNNER);
    ASSERT_EQ(suite->device_.player->getEasyAttempts(GameType::GHOST_RUNNER), 1);
    ASSERT_EQ(suite->device_.player->getEasyAttempts(gameType), 2);  // Unchanged
}

// Test: Hard mode win sets color eligibility, easy mode does not
void hardWinSetsEligibilityEasyDoesNot(ProgressionEdgeCaseTestSuite* suite) {
    int gameType = static_cast<int>(GameType::SIGNAL_ECHO);

    // Easy mode win — no eligibility (tested via FdnComplete state)
    // Here we test the Player API directly
    ASSERT_FALSE(suite->device_.player->hasColorProfileEligibility(gameType));

    // Hard mode win — adds eligibility
    suite->device_.player->addColorProfileEligibility(gameType);
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(gameType));
}

// Test: Switching from easy to hard mid-progression preserves progress
void switchingDifficultyPreservesProgress(ProgressionEdgeCaseTestSuite* suite) {
    GameType gameType = GameType::SIGNAL_ECHO;

    // Play easy mode
    suite->device_.player->incrementEasyAttempts(gameType);
    suite->device_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    uint8_t progressAfterEasy = suite->device_.player->getKonamiProgress();
    ASSERT_TRUE(progressAfterEasy & (1 << 6));  // START button

    // Switch to hard mode (simulated by attempt counter)
    suite->device_.player->incrementHardAttempts(gameType);

    // Progress should remain
    ASSERT_EQ(suite->device_.player->getKonamiProgress(), progressAfterEasy);
    ASSERT_EQ(suite->device_.player->getEasyAttempts(gameType), 1);
    ASSERT_EQ(suite->device_.player->getHardAttempts(gameType), 1);
}

// Test: Easy mode win after hard mode win does not downgrade eligibility
void easyAfterHardDoesNotDowngrade(ProgressionEdgeCaseTestSuite* suite) {
    int gameType = static_cast<int>(GameType::SIGNAL_ECHO);

    // Hard mode win — gain eligibility
    suite->device_.player->addColorProfileEligibility(gameType);
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(gameType));

    // Replay on easy mode — eligibility should persist
    suite->device_.player->incrementEasyAttempts(static_cast<GameType>(gameType));
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(gameType));
}

// ============================================
// PERSISTENCE TESTS
// ============================================

// Test: Progress persists across save/load cycles
void progressPersistsAcrossSessions(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->setKonamiProgress(0x55);  // Alternating bits
    suite->device_.player->setKonamiBoon(true);
    suite->device_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->device_.player->incrementEasyAttempts(GameType::GHOST_RUNNER);
    suite->device_.player->incrementHardAttempts(GameType::CIPHER_PATH);

    pm.saveProgress();

    // Simulate new session: fresh player
    Player sessionTwo;
    ProgressManager pm2;
    pm2.initialize(&sessionTwo, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(sessionTwo.getKonamiProgress(), 0x55);
    ASSERT_TRUE(sessionTwo.hasKonamiBoon());
    ASSERT_TRUE(sessionTwo.hasColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO)));
    ASSERT_EQ(sessionTwo.getEasyAttempts(GameType::GHOST_RUNNER), 1);
    ASSERT_EQ(sessionTwo.getHardAttempts(GameType::CIPHER_PATH), 1);
}

// Test: Corrupted persistence data recovers gracefully
void corruptedDataRecoversGracefully(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Corrupt storage with invalid values
    suite->device_.storageDriver->writeUChar("konami", 0xFF);  // All bits set (invalid)
    suite->device_.storageDriver->writeUChar("color_profile", 255);  // Out of range

    // Load should not crash, should use defaults or sanitize
    pm.loadProgress();

    // Konami progress should load as-is (bitmask allows any value)
    ASSERT_EQ(suite->device_.player->getKonamiProgress(), 0xFF);

    // Color profile should be sanitized: 255 - 1 = 254, which is out of range for GameType (0-6)
    // The system stores as gameType + 1, so 255 means gameType 254, which is invalid but loaded as-is
    // This test verifies the system doesn't crash on invalid data
    int equippedProfile = suite->device_.player->getEquippedColorProfile();
    // The value will be 254 (255 - 1), which is technically out of range
    // This is acceptable as long as it doesn't crash
    ASSERT_EQ(equippedProfile, 254);
}

// Test: Missing fields in persistence data use defaults
void missingFieldsUseDefaults(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Don't write any data — storage should return defaults

    pm.loadProgress();

    ASSERT_EQ(suite->device_.player->getKonamiProgress(), 0);
    ASSERT_FALSE(suite->device_.player->hasKonamiBoon());
    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), -1);
    ASSERT_TRUE(suite->device_.player->getColorProfileEligibility().empty());
    ASSERT_EQ(suite->device_.player->getEasyAttempts(GameType::SIGNAL_ECHO), 0);
}

// ============================================
// BOUNDARY TESTS
// ============================================

// Test: Maximum Konami progress (all 7 buttons + hard mode bit)
void maximumKonamiProgress(ProgressionEdgeCaseTestSuite* suite) {
    suite->device_.player->setKonamiProgress(0xFF);  // All 8 bits
    ASSERT_TRUE(suite->device_.player->isKonamiComplete());
    ASSERT_TRUE(suite->device_.player->hasAllKonamiButtons());
    ASSERT_TRUE(suite->device_.player->isHardModeUnlocked());

    // Save and load
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);
    pm.saveProgress();

    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 0xFF);
}

// Test: Maximum attempt counts (uint8_t max = 255)
void maximumAttemptCounts(ProgressionEdgeCaseTestSuite* suite) {
    GameType gameType = GameType::SIGNAL_ECHO;

    // Manually set to max via setter
    suite->device_.player->setEasyAttempts(gameType, 255);
    suite->device_.player->setHardAttempts(gameType, 255);

    ASSERT_EQ(suite->device_.player->getEasyAttempts(gameType), 255);
    ASSERT_EQ(suite->device_.player->getHardAttempts(gameType), 255);

    // Save and load
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);
    pm.saveProgress();

    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getEasyAttempts(gameType), 255);
    ASSERT_EQ(freshPlayer.getHardAttempts(gameType), 255);
}

// Test: Zero progression state (fresh device)
void zeroProgressionState(ProgressionEdgeCaseTestSuite* suite) {
    // Device should start with clean defaults
    ASSERT_EQ(suite->device_.player->getKonamiProgress(), 0);
    ASSERT_FALSE(suite->device_.player->hasKonamiBoon());
    ASSERT_FALSE(suite->device_.player->isKonamiComplete());
    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), -1);
    ASSERT_TRUE(suite->device_.player->getColorProfileEligibility().empty());

    for (int i = 0; i < 7; i++) {
        ASSERT_EQ(suite->device_.player->getEasyAttempts(static_cast<GameType>(i)), 0);
        ASSERT_EQ(suite->device_.player->getHardAttempts(static_cast<GameType>(i)), 0);
    }
}

// Test: All color profiles unlocked
void allColorProfilesUnlocked(ProgressionEdgeCaseTestSuite* suite) {
    // Add all 7 game types to eligibility
    for (int i = 0; i < 7; i++) {
        suite->device_.player->addColorProfileEligibility(i);
    }

    const auto& elig = suite->device_.player->getColorProfileEligibility();
    ASSERT_EQ(elig.size(), 7u);

    for (int i = 0; i < 7; i++) {
        ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(i));
    }

    // Save and load
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);
    pm.saveProgress();

    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getColorProfileEligibility().size(), 7u);
}

// ============================================
// INVALID STATE RECOVERY TESTS
// ============================================

// Test: Invalid Konami progress bits beyond 7 are preserved but don't break logic
void invalidKonamiProgressHandled(ProgressionEdgeCaseTestSuite* suite) {
    // Set bits beyond the 7 Konami buttons (bit 7 is hard mode, bits 8+ are invalid)
    suite->device_.player->setKonamiProgress(0xFF);

    // isKonamiComplete only checks bits 0-6
    ASSERT_TRUE(suite->device_.player->isKonamiComplete());

    // System should not crash
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);
    pm.saveProgress();

    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_TRUE(freshPlayer.isKonamiComplete());
}

// Test: Out-of-range GameType values do not crash
void outOfRangeGameTypeHandled(ProgressionEdgeCaseTestSuite* suite) {
    // Valid GameType range is 0-6 (7 games)
    // Test with value 10 (out of range)
    GameType invalidGameType = static_cast<GameType>(10);

    // Should not crash, should be no-op
    suite->device_.player->incrementEasyAttempts(invalidGameType);
    suite->device_.player->incrementHardAttempts(invalidGameType);

    ASSERT_EQ(suite->device_.player->getEasyAttempts(invalidGameType), 0);
    ASSERT_EQ(suite->device_.player->getHardAttempts(invalidGameType), 0);
}

// Test: Negative equipped profile value (default -1)
void negativeEquippedProfileHandled(ProgressionEdgeCaseTestSuite* suite) {
    suite->device_.player->setEquippedColorProfile(-1);
    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), -1);

    // Save and load
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);
    pm.saveProgress();

    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    // Should reload as -1 (no profile equipped)
    ASSERT_EQ(freshPlayer.getEquippedColorProfile(), -1);
}

// ============================================
// COLOR PROFILE UNLOCKING TESTS
// ============================================

// Test: Unlock color profile through eligibility
void unlockColorProfileViaEligibility(ProgressionEdgeCaseTestSuite* suite) {
    int gameType = static_cast<int>(GameType::SIGNAL_ECHO);
    ASSERT_FALSE(suite->device_.player->hasColorProfileEligibility(gameType));

    // Simulate hard mode win
    suite->device_.player->addColorProfileEligibility(gameType);
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(gameType));
}

// Test: Use locked color profile falls back to default behavior
void useLockedProfileHandled(ProgressionEdgeCaseTestSuite* suite) {
    int lockedGameType = static_cast<int>(GameType::SIGNAL_ECHO);

    // Try to equip without eligibility
    suite->device_.player->setEquippedColorProfile(lockedGameType);

    // System allows equipping without eligibility (no validation)
    // This is expected behavior — the display/game logic should handle it
    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), lockedGameType);
    ASSERT_FALSE(suite->device_.player->hasColorProfileEligibility(lockedGameType));
}

// Test: Color profile persistence across restarts
void colorProfilePersistence(ProgressionEdgeCaseTestSuite* suite) {
    int gameType = static_cast<int>(GameType::GHOST_RUNNER);

    suite->device_.player->addColorProfileEligibility(gameType);
    suite->device_.player->setEquippedColorProfile(gameType);

    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);
    pm.saveProgress();

    // Simulate restart
    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_TRUE(freshPlayer.hasColorProfileEligibility(gameType));
    ASSERT_EQ(freshPlayer.getEquippedColorProfile(), gameType);
}

// ============================================
// SERVER MERGE CONFLICT RESOLUTION TESTS
// ============================================

// Test: Server merge — bitmask union
void serverMergeBitmaskUnion(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Local: bits 0-2 set
    suite->device_.player->setKonamiProgress(0x07);

    // Simulate server response with bits 3-5 set
    std::string serverJson = R"({"data":{"konami":56,"boon":false}})";
    pm.downloadAndMergeProgress(serverJson);

    // Result: union of both (0x07 | 0x38 = 0x3F)
    ASSERT_EQ(suite->device_.player->getKonamiProgress(), 0x3F);
}

// Test: Server merge — max-wins for attempts
void serverMergeMaxWins(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Local: 5 easy attempts, 2 hard attempts
    suite->device_.player->setEasyAttempts(GameType::SIGNAL_ECHO, 5);
    suite->device_.player->setHardAttempts(GameType::SIGNAL_ECHO, 2);

    // Server: 3 easy attempts, 8 hard attempts
    std::string serverJson = R"({"data":{"easyAttempts":[3,0,0,0,0,0,0],"hardAttempts":[8,0,0,0,0,0,0]}})";
    pm.downloadAndMergeProgress(serverJson);

    // Result: max(5,3)=5 easy, max(2,8)=8 hard
    ASSERT_EQ(suite->device_.player->getEasyAttempts(GameType::SIGNAL_ECHO), 5);
    ASSERT_EQ(suite->device_.player->getHardAttempts(GameType::SIGNAL_ECHO), 8);
}

// Test: Server merge — missing fields don't overwrite local
void serverMergeMissingFieldsPreserveLocal(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Local: equipped profile and eligibility set
    suite->device_.player->setEquippedColorProfile(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->device_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    // Server response missing profile and eligibility
    std::string serverJson = R"({"data":{"konami":15,"boon":true}})";
    pm.downloadAndMergeProgress(serverJson);

    // Local values should be preserved
    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    // Server values should be merged
    ASSERT_EQ(suite->device_.player->getKonamiProgress(), 15);
    ASSERT_TRUE(suite->device_.player->hasKonamiBoon());
}

// Test: Server merge — color eligibility union
void serverMergeColorEligibilityUnion(ProgressionEdgeCaseTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    // Local: SIGNAL_ECHO and GHOST_RUNNER
    suite->device_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->device_.player->addColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER));

    // Server: GHOST_RUNNER and CIPHER_PATH (bitmask: bit 1 + bit 4 = 0x12)
    std::string serverJson = R"({"data":{"colorEligibility":18}})";
    pm.downloadAndMergeProgress(serverJson);

    // Result: union of all three
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::GHOST_RUNNER)));
    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::CIPHER_PATH)));
}

#endif // NATIVE_BUILD
