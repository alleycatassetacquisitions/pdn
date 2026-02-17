#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "cli/cli-commands.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/minigame.hpp"
#include "game/progress-manager.hpp"
#include "device/device-constants.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// DIFFICULTY GATING TEST SUITE
// ============================================

class DifficultyGatingTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        player_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::SIGNAL_ECHO);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(player_);

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void tickPlayerWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            player_.pdn->loop();
        }
    }

    void advanceToIdle() {
        player_.game->skipToState(player_.pdn, 6);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentStateId();
    }

    SignalEcho* getSignalEcho() {
        return dynamic_cast<SignalEcho*>(
            player_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    }

    DeviceInstance player_;
    DeviceInstance fdn_;
};

// Test: First encounter (no boon) → EASY mode
void difficultyGatingEasyWithoutBoon(DifficultyGatingTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasKonamiBoon());

    // Trigger FDN detection + handshake
    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickPlayerWithTime(5, 100);

    // Signal Echo should be in EASY mode
    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_EASY.sequenceLength);
    ASSERT_EQ(echo->getConfig().allowedMistakes, SIGNAL_ECHO_EASY.allowedMistakes);
    ASSERT_TRUE(echo->getConfig().managedMode);
}

// Test: After beating EASY (has button) → re-encounter → choose HARD
void difficultyGatingHardWithBoon(DifficultyGatingTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickPlayerWithTime(5, 100);

    // Should be in FdnReencounter, not directly in game
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD (index 0, confirm with PRIMARY)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_HARD.sequenceLength);
    ASSERT_EQ(echo->getConfig().allowedMistakes, SIGNAL_ECHO_HARD.allowedMistakes);
    ASSERT_TRUE(echo->getConfig().managedMode);
}

// Test: FdnDetected sets lastFdnGameType on player
void difficultyGatingSetsLastGameType(DifficultyGatingTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_EQ(suite->player_.player->getLastFdnGameType(), -1);

    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickPlayerWithTime(5, 100);

    ASSERT_EQ(suite->player_.player->getLastFdnGameType(),
              static_cast<int>(GameType::SIGNAL_ECHO));
}

// Test: Unknown game type → transitions back to idle
void difficultyGatingUnknownGameIdle(DifficultyGatingTestSuite* suite) {
    suite->advanceToIdle();

    // GameType::QUICKDRAW has no registered app (getAppIdForGame returns -1)
    suite->player_.serialOutDriver->injectInput("*fdn:0:0\r");
    suite->tick(3);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickPlayerWithTime(5, 100);

    // Should fall back to Idle since no app for QUICKDRAW game type
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
}

// ============================================
// AUTO-BOON TEST SUITE
// ============================================

class AutoBoonTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            device_.clockDriver->advance(delayMs);
            device_.pdn->loop();
        }
    }

    void setupWinOutcome(bool hardMode = false) {
        auto* echo = dynamic_cast<MiniGame*>(
            device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
        MiniGameOutcome outcome;
        outcome.result = MiniGameResult::WON;
        outcome.score = 100;
        outcome.hardMode = hardMode;
        echo->setOutcome(outcome);
    }

    DeviceInstance device_;
};

// Test: Auto-boon triggers when 7th button is unlocked
void autoBoonTriggersOnSeventhButton(AutoBoonTestSuite* suite) {
    // Set 6 of 7 buttons (all except START)
    suite->device_.player->setKonamiProgress(0x3F);  // bits 0-5
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));
    ASSERT_FALSE(suite->device_.player->isKonamiComplete());
    ASSERT_FALSE(suite->device_.player->hasKonamiBoon());

    suite->setupWinOutcome();
    suite->device_.player->setPendingChallenge("fdn:7:6");

    // Skip to FdnComplete (stateMap index 22)
    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    // The win unlocks START (bit 6), completing all 7
    ASSERT_TRUE(suite->device_.player->isKonamiComplete());
    ASSERT_TRUE(suite->device_.player->hasKonamiBoon());
}

// Test: Auto-boon does NOT trigger if already has boon
void autoBoonDoesNotRetrigger(AutoBoonTestSuite* suite) {
    suite->device_.player->setKonamiProgress(0x7F);
    suite->device_.player->setKonamiBoon(true);
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->setupWinOutcome();
    suite->device_.player->setPendingChallenge("fdn:7:6");

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    // Still has boon, but no KONAMI COMPLETE message on display
    ASSERT_TRUE(suite->device_.player->hasKonamiBoon());
    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundKonamiComplete = false;
    for (const auto& entry : textHistory) {
        if (entry.find("KONAMI COMPLETE!") != std::string::npos) {
            foundKonamiComplete = true;
        }
    }
    ASSERT_FALSE(foundKonamiComplete);
}

// Test: Display shows KONAMI COMPLETE! when boon first triggers
void autoBoonShowsDisplay(AutoBoonTestSuite* suite) {
    suite->device_.player->setKonamiProgress(0x3F);  // 6 of 7
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->setupWinOutcome();
    suite->device_.player->setPendingChallenge("fdn:7:6");

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundKonamiComplete = false;
    for (const auto& entry : textHistory) {
        if (entry.find("KONAMI COMPLETE!") != std::string::npos) {
            foundKonamiComplete = true;
        }
    }
    ASSERT_TRUE(foundKonamiComplete);
}

// Test: Hard mode win sets pendingProfileGame
void autoBoonHardWinSetsPending(AutoBoonTestSuite* suite) {
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->setupWinOutcome(true);  // hardMode = true
    suite->device_.player->setPendingChallenge("fdn:7:6");

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    ASSERT_EQ(suite->device_.player->getPendingProfileGame(),
              static_cast<int>(GameType::SIGNAL_ECHO));
}

// Test: Easy mode win does NOT set pendingProfileGame
void autoBoonEasyWinNoPending(AutoBoonTestSuite* suite) {
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->setupWinOutcome(false);  // hardMode = false
    suite->device_.player->setPendingChallenge("fdn:7:6");

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    ASSERT_EQ(suite->device_.player->getPendingProfileGame(), -1);
}

// ============================================
// PLAYER FIELD ACCESSORS TEST SUITE
// ============================================

// Test: isKonamiComplete when all 7 bits set
void playerIsKonamiComplete(PlayerChallengeTestSuite* suite) {
    ASSERT_FALSE(suite->player_->isKonamiComplete());
    suite->player_->setKonamiProgress(0x7F);
    ASSERT_TRUE(suite->player_->isKonamiComplete());
    // Extra bits beyond 7 don't affect it
    suite->player_->setKonamiProgress(0xFF);
    ASSERT_TRUE(suite->player_->isKonamiComplete());
}

// Test: konamiBoon set/get
void playerKonamiBoonSetGet(PlayerChallengeTestSuite* suite) {
    ASSERT_FALSE(suite->player_->hasKonamiBoon());
    suite->player_->setKonamiBoon(true);
    ASSERT_TRUE(suite->player_->hasKonamiBoon());
    suite->player_->setKonamiBoon(false);
    ASSERT_FALSE(suite->player_->hasKonamiBoon());
}

// Test: lastFdnGameType set/get
void playerLastFdnGameTypeSetGet(PlayerChallengeTestSuite* suite) {
    ASSERT_EQ(suite->player_->getLastFdnGameType(), -1);
    suite->player_->setLastFdnGameType(7);
    ASSERT_EQ(suite->player_->getLastFdnGameType(), 7);
}

// Test: pendingProfileGame set/get
void playerPendingProfileGameSetGet(PlayerChallengeTestSuite* suite) {
    ASSERT_EQ(suite->player_->getPendingProfileGame(), -1);
    suite->player_->setPendingProfileGame(7);
    ASSERT_EQ(suite->player_->getPendingProfileGame(), 7);
}

// Test: getColorProfileEligibility returns the set
void playerColorProfileEligibilitySet(PlayerChallengeTestSuite* suite) {
    ASSERT_TRUE(suite->player_->getColorProfileEligibility().empty());
    suite->player_->addColorProfileEligibility(7);
    suite->player_->addColorProfileEligibility(3);
    const auto& elig = suite->player_->getColorProfileEligibility();
    ASSERT_EQ(elig.size(), 2u);
    ASSERT_TRUE(elig.count(7) > 0);
    ASSERT_TRUE(elig.count(3) > 0);
}

// ============================================
// PROGRESS MANAGER EXTENDED TESTS
// ============================================

// Test: ProgressManager saves and loads konamiBoon
void progressManagerSaveLoadBoon(ProgressManagerTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->setKonamiBoon(true);
    pm.saveProgress();

    // Reset player and reload
    suite->device_.player->setKonamiBoon(false);
    ASSERT_FALSE(suite->device_.player->hasKonamiBoon());

    pm.loadProgress();
    ASSERT_TRUE(suite->device_.player->hasKonamiBoon());
}

// Test: ProgressManager saves and loads equippedColorProfile
void progressManagerSaveLoadProfile(ProgressManagerTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->setEquippedColorProfile(7);
    pm.saveProgress();

    suite->device_.player->setEquippedColorProfile(-1);
    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), -1);

    pm.loadProgress();
    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), 7);
}

// Test: ProgressManager saves and loads colorProfileEligibility
void progressManagerSaveLoadEligibility(ProgressManagerTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->addColorProfileEligibility(3);
    suite->device_.player->addColorProfileEligibility(7);
    pm.saveProgress();

    // Create a fresh player to load into
    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_TRUE(freshPlayer.hasColorProfileEligibility(3));
    ASSERT_TRUE(freshPlayer.hasColorProfileEligibility(7));
    ASSERT_FALSE(freshPlayer.hasColorProfileEligibility(1));
}

// Test: ProgressManager clear resets all new fields
void progressManagerClearAll(ProgressManagerTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->setKonamiProgress(0x7F);
    suite->device_.player->setKonamiBoon(true);
    suite->device_.player->setEquippedColorProfile(7);
    suite->device_.player->addColorProfileEligibility(7);
    pm.saveProgress();
    pm.clearProgress();

    // Load into fresh player
    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->device_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 0);
    ASSERT_FALSE(freshPlayer.hasKonamiBoon());
    ASSERT_EQ(freshPlayer.getEquippedColorProfile(), -1);
    ASSERT_TRUE(freshPlayer.getColorProfileEligibility().empty());
}

// ============================================
// SERVER SYNC TESTS
// ============================================

// Test: syncProgress sends PUT /api/progress
void progressManagerSyncCallsServer(ProgressManagerTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->setKonamiProgress(0x7F);
    suite->device_.player->setKonamiBoon(true);
    pm.saveProgress();
    ASSERT_TRUE(pm.hasUnsyncedProgress());

    // Sync to server
    pm.syncProgress(suite->device_.httpClientDriver);

    // Process the queued request
    suite->device_.httpClientDriver->exec();

    // Verify request was made
    auto& history = suite->device_.httpClientDriver->getRequestHistory();
    ASSERT_FALSE(history.empty());

    bool foundProgress = false;
    for (const auto& entry : history) {
        if (entry.path == "/api/progress" && entry.method == "PUT") {
            foundProgress = true;
            break;
        }
    }
    ASSERT_TRUE(foundProgress);
}

// ============================================
// GETAPPIDFOREGAME TESTS
// ============================================

class GameRoutingTestSuite : public testing::Test {};

// Test: getAppIdForGame returns correct IDs
void gameRoutingSignalEcho(GameRoutingTestSuite* /*suite*/) {
    ASSERT_EQ(getAppIdForGame(GameType::SIGNAL_ECHO), 2);
    ASSERT_EQ(getAppIdForGame(GameType::FIREWALL_DECRYPT), 3);
    ASSERT_EQ(getAppIdForGame(GameType::QUICKDRAW), -1);
    ASSERT_EQ(getAppIdForGame(GameType::GHOST_RUNNER), 4);
    ASSERT_EQ(getAppIdForGame(GameType::SPIKE_VECTOR), 5);
    ASSERT_EQ(getAppIdForGame(GameType::CIPHER_PATH), 6);
    ASSERT_EQ(getAppIdForGame(GameType::EXPLOIT_SEQUENCER), 7);
    ASSERT_EQ(getAppIdForGame(GameType::BREACH_DEFENSE), 8);
}

// ============================================
// KONAMI COMMAND TESTS
// ============================================

class KonamiCommandTestSuite : public testing::Test {
public:
    void SetUp() override {
        devices_.push_back(DeviceFactory::createDevice(0, true));
        SimpleTimer::setPlatformClock(devices_[0].clockDriver);
    }

    void TearDown() override {
        for (auto& dev : devices_) {
            DeviceFactory::destroyDevice(dev);
        }
        devices_.clear();
    }

    std::vector<DeviceInstance> devices_;
    Renderer renderer_;
    CommandProcessor processor_;
};

// Test: konami command shows current progress
void konamiCommandShowsProgress(KonamiCommandTestSuite* suite) {
    suite->devices_[0].player->setKonamiProgress(0x03);  // UP + DOWN
    int selected = 0;
    auto result = suite->processor_.execute("konami", suite->devices_, selected, suite->renderer_);
    ASSERT_TRUE(result.message.find("0x03") != std::string::npos);
    ASSERT_TRUE(result.message.find("2/7") != std::string::npos);
}

// Test: konami command sets progress
void konamiCommandSetsProgress(KonamiCommandTestSuite* suite) {
    int selected = 0;
    auto result = suite->processor_.execute("konami 127", suite->devices_, selected, suite->renderer_);
    ASSERT_EQ(suite->devices_[0].player->getKonamiProgress(), 127);
    ASSERT_TRUE(suite->devices_[0].player->hasKonamiBoon());
    ASSERT_TRUE(result.message.find("7/7") != std::string::npos);
    ASSERT_TRUE(result.message.find("boon=yes") != std::string::npos);
}

#endif // NATIVE_BUILD
