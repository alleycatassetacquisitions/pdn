#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
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
// FDN INTEGRATION TEST SUITE
// ============================================

class FdnIntegrationTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Device 0 = player (hunter), Device 1 = FDN NPC
        player_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::SIGNAL_ECHO);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            fdn_.clockDriver->advance(delayMs);
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

    // Advance player to Idle state (skip to it directly)
    void advanceToIdle() {
        // stateMap index 7 = Idle (in populateStateMap order)
        player_.game->skipToState(player_.pdn, 7);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentState()->getStateId();
    }

    int getFdnStateId() {
        return fdn_.game->getCurrentState()->getStateId();
    }

    void connectCable() {
        SerialCableBroker::getInstance().connect(0, 1);
    }

    DeviceInstance player_;
    DeviceInstance fdn_;
};

// ============================================
// FDN DETECTION TESTS
//
// These tests simulate the serial handshake by directly injecting
// messages into the player's PRIMARY jack (outputJack for hunters).
// The SerialCableBroker can't route output↔output (both FDN and
// hunter use OUTPUT_JACK as PRIMARY), so we inject directly.
// ============================================

// Test: Player in Idle detects FDN broadcast and transitions to FdnDetected
void fdnIntegrationDetectsFdnBroadcast(FdnIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Simulate receiving an FDN broadcast on the player's PRIMARY jack
    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);

    // Player should have detected the FDN and transitioned
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);
}

// Test: FdnDetected sends MAC address on mount
void fdnIntegrationHandshakeSendsMAC(FdnIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN detection
    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Check that smac was sent by the player
    auto& history = suite->player_.serialOutDriver->getSentHistory();
    bool macSent = false;
    for (const auto& msg : history) {
        if (msg.rfind("smac", 0) == 0) {
            macSent = true;
            break;
        }
    }
    ASSERT_TRUE(macSent);
}

// Test: After fack received, player switches to Signal Echo app
void fdnIntegrationTransitionsToSignalEcho(FdnIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN detection
    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Inject fack from NPC — this triggers the app switch
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickPlayerWithTime(5, 100);

    // After handshake, player should have switched to Signal Echo
    // The active app is now Signal Echo, so getPlayerStateId() returns
    // the game pointer's state (Quickdraw), but the device is running Echo.
    // Check Signal Echo via getApp:
    auto* echo = suite->player_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID));
    ASSERT_NE(echo, nullptr);
    int echoStateId = echo->getCurrentState()->getStateId();
    ASSERT_GE(echoStateId, ECHO_INTRO);
}

// Test: FdnDetected times out if no fack received
void fdnIntegrationHandshakeTimeout(FdnIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    // Inject an FDN message on the player's PRIMARY jack
    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Wait for timeout (10 seconds)
    suite->tickPlayerWithTime(60, 200);

    // Should transition back to Idle after timeout
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
}

// ============================================
// FDN COMPLETE TESTS
// ============================================

class FdnCompleteTestSuite : public testing::Test {
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

    DeviceInstance device_;
};

// Test: FdnComplete shows VICTORY on win
void fdnCompleteShowsVictoryOnWin(FdnCompleteTestSuite* suite) {
    // Set up Signal Echo with a won outcome
    auto* echo = dynamic_cast<MiniGame*>(
        suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(echo, nullptr);

    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::WON;
    outcome.score = 42;
    outcome.hardMode = true;
    echo->setOutcome(outcome);

    // Set pending challenge and last game type on player
    suite->device_.player->setPendingChallenge("fdn:7:6");
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    // Skip Quickdraw to the FdnComplete state
    // stateMap order: playerReg(0), fetchUser(1), confirmOffline(2), chooseRole(3),
    // allegiancePicker(4), welcome(5), awaken(6), idle(7), handshakeInit(8),
    // bountySendCC(9), hunterSendId(10), connSuccessful(11), duelCountdown(12),
    // duel(13), duelPushed(14), duelReceivedResult(15), duelResult(16),
    // win(17), lose(18), uploadMatches(19), sleep(20), fdnDetected(21), fdnComplete(22)
    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    // Check display shows VICTORY
    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundVictory = false;
    for (const auto& entry : textHistory) {
        if (entry.find("VICTORY!") != std::string::npos) {
            foundVictory = true;
            break;
        }
    }
    ASSERT_TRUE(foundVictory);
}

// Test: FdnComplete unlocks Konami button on win
void fdnCompleteUnlocksKonamiOnWin(FdnCompleteTestSuite* suite) {
    auto* echo = dynamic_cast<MiniGame*>(
        suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(echo, nullptr);

    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::WON;
    outcome.score = 10;
    outcome.hardMode = false;
    echo->setOutcome(outcome);

    suite->device_.player->setPendingChallenge("fdn:7:6");
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    // Verify button not unlocked before
    ASSERT_FALSE(suite->device_.player->hasUnlockedButton(
        static_cast<uint8_t>(KonamiButton::START)));

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    // Signal Echo = GameType 7, reward = UP(0)
    ASSERT_TRUE(suite->device_.player->hasUnlockedButton(
        static_cast<uint8_t>(KonamiButton::UP)));
}

// Test: FdnComplete unlocks color profile on hard mode win
void fdnCompleteUnlocksColorProfileOnHardWin(FdnCompleteTestSuite* suite) {
    auto* echo = dynamic_cast<MiniGame*>(
        suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(echo, nullptr);

    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::WON;
    outcome.score = 20;
    outcome.hardMode = true;
    echo->setOutcome(outcome);

    suite->device_.player->setPendingChallenge("fdn:7:6");
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    ASSERT_FALSE(suite->device_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));
}

// Test: FdnComplete shows DEFEATED on loss
void fdnCompleteShowsDefeatedOnLoss(FdnCompleteTestSuite* suite) {
    auto* echo = dynamic_cast<MiniGame*>(
        suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(echo, nullptr);

    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::LOST;
    outcome.score = 5;
    echo->setOutcome(outcome);

    suite->device_.player->setPendingChallenge("fdn:7:6");
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundDefeated = false;
    for (const auto& entry : textHistory) {
        if (entry.find("DEFEATED") != std::string::npos) {
            foundDefeated = true;
            break;
        }
    }
    ASSERT_TRUE(foundDefeated);

    // Should NOT unlock Konami button on loss
    ASSERT_FALSE(suite->device_.player->hasUnlockedButton(
        static_cast<uint8_t>(KonamiButton::START)));
}

// Test: FdnComplete transitions to Idle after timer
void fdnCompleteTransitionsToIdleAfterTimer(FdnCompleteTestSuite* suite) {
    auto* echo = dynamic_cast<MiniGame*>(
        suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(echo, nullptr);

    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::WON;
    outcome.score = 10;
    echo->setOutcome(outcome);

    suite->device_.player->setPendingChallenge("fdn:7:6");
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    // Should be at FdnComplete
    int stateId = suite->device_.game->getCurrentState()->getStateId();
    ASSERT_EQ(stateId, FDN_COMPLETE);

    // Wait for display timer (3 seconds)
    suite->tickWithTime(20, 200);

    // Should have transitioned back to Idle
    stateId = suite->device_.game->getCurrentState()->getStateId();
    ASSERT_EQ(stateId, IDLE);
}

// Test: FdnComplete clears pending challenge on dismount
void fdnCompleteClearsPendingChallenge(FdnCompleteTestSuite* suite) {
    auto* echo = dynamic_cast<MiniGame*>(
        suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(echo, nullptr);

    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::LOST;
    outcome.score = 0;
    echo->setOutcome(outcome);

    suite->device_.player->setPendingChallenge("fdn:7:6");
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));
    ASSERT_TRUE(suite->device_.player->hasPendingChallenge());

    suite->device_.game->skipToState(suite->device_.pdn, 23);
    suite->tick(1);

    // Wait for transition to Idle (clears pending challenge on dismount)
    suite->tickWithTime(20, 200);

    ASSERT_FALSE(suite->device_.player->hasPendingChallenge());
}

// ============================================
// PROGRESS MANAGER TESTS
// ============================================

class ProgressManagerTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
    }

    DeviceInstance device_;
};

// Test: Progress is saved to NVS on win
void progressManagerSavesOnWin(ProgressManagerTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->unlockKonamiButton(0);  // UP
    suite->device_.player->unlockKonamiButton(6);  // START
    pm.saveProgress();

    uint8_t stored = suite->device_.storageDriver->readUChar("konami", 0);
    ASSERT_EQ(stored, suite->device_.player->getKonamiProgress());
    ASSERT_TRUE(pm.hasUnsyncedProgress());
}

// Test: Progress is loaded from NVS
void progressManagerLoadsProgress(ProgressManagerTestSuite* suite) {
    // Write directly to storage
    suite->device_.storageDriver->writeUChar("konami", 0b01000011);  // UP + DOWN + START

    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);
    pm.loadProgress();

    ASSERT_EQ(suite->device_.player->getKonamiProgress(), 0b01000011);
    ASSERT_TRUE(suite->device_.player->hasUnlockedButton(0));   // UP
    ASSERT_TRUE(suite->device_.player->hasUnlockedButton(1));   // DOWN
    ASSERT_TRUE(suite->device_.player->hasUnlockedButton(6));   // START
    ASSERT_FALSE(suite->device_.player->hasUnlockedButton(2));  // LEFT
}

// Test: Clear progress resets storage
void progressManagerClearsProgress(ProgressManagerTestSuite* suite) {
    ProgressManager pm;
    pm.initialize(suite->device_.player, suite->device_.storageDriver);

    suite->device_.player->unlockKonamiButton(0);
    pm.saveProgress();
    ASSERT_TRUE(pm.hasUnsyncedProgress());

    pm.clearProgress();
    ASSERT_FALSE(pm.hasUnsyncedProgress());

    uint8_t stored = suite->device_.storageDriver->readUChar("konami", 99);
    ASSERT_EQ(stored, 0);
}

// ============================================
// PLAYER CHALLENGE FIELDS TESTS
// ============================================

class PlayerChallengeTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = new Player();
    }

    void TearDown() override {
        delete player_;
    }

    Player* player_;
};

// Test: Konami button unlock and query
void playerKonamiUnlockAndQuery(PlayerChallengeTestSuite* suite) {
    ASSERT_EQ(suite->player_->getKonamiProgress(), 0);
    ASSERT_FALSE(suite->player_->hasUnlockedButton(0));

    suite->player_->unlockKonamiButton(0);
    ASSERT_TRUE(suite->player_->hasUnlockedButton(0));
    ASSERT_FALSE(suite->player_->hasUnlockedButton(1));

    suite->player_->unlockKonamiButton(6);
    ASSERT_TRUE(suite->player_->hasUnlockedButton(6));
    ASSERT_EQ(suite->player_->getKonamiProgress(), 0b01000001);
}

// Test: Pending challenge set/clear
void playerPendingChallengeSetClear(PlayerChallengeTestSuite* suite) {
    ASSERT_FALSE(suite->player_->hasPendingChallenge());

    suite->player_->setPendingChallenge("fdn:7:6");
    ASSERT_TRUE(suite->player_->hasPendingChallenge());
    ASSERT_EQ(suite->player_->getPendingCdevMessage(), "fdn:7:6");

    suite->player_->clearPendingChallenge();
    ASSERT_FALSE(suite->player_->hasPendingChallenge());
}

// Test: Color profile eligibility
void playerColorProfileEligibility(PlayerChallengeTestSuite* suite) {
    ASSERT_FALSE(suite->player_->hasColorProfileEligibility(7));

    suite->player_->addColorProfileEligibility(7);
    ASSERT_TRUE(suite->player_->hasColorProfileEligibility(7));
    ASSERT_FALSE(suite->player_->hasColorProfileEligibility(1));
}

// Test: Equipped color profile
void playerEquippedColorProfile(PlayerChallengeTestSuite* suite) {
    ASSERT_EQ(suite->player_->getEquippedColorProfile(), -1);

    suite->player_->setEquippedColorProfile(7);
    ASSERT_EQ(suite->player_->getEquippedColorProfile(), 7);
}

// Test: Konami progress set/get
void playerKonamiProgressSetGet(PlayerChallengeTestSuite* suite) {
    suite->player_->setKonamiProgress(0xFF);
    ASSERT_EQ(suite->player_->getKonamiProgress(), 0xFF);
    for (int i = 0; i < 7; i++) {
        ASSERT_TRUE(suite->player_->hasUnlockedButton(i));
    }
}

// ============================================
// APP SWITCHING TESTS (FDN → Signal Echo → Quickdraw)
// ============================================

class AppSwitchingTestSuite : public testing::Test {
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

    DeviceInstance device_;
};

// Test: Signal Echo is registered and accessible via getApp
void appSwitchingSignalEchoRegistered(AppSwitchingTestSuite* suite) {
    auto* app = suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID));
    ASSERT_NE(app, nullptr);

    auto* echo = dynamic_cast<MiniGame*>(app);
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getGameType(), GameType::SIGNAL_ECHO);
}

// Test: Quickdraw is the active app at start
void appSwitchingQuickdrawActiveAtStart(AppSwitchingTestSuite* suite) {
    // The game pointer points to the Quickdraw instance
    ASSERT_NE(suite->device_.game, nullptr);

    // Its current state should be in the Quickdraw range (0-22)
    int stateId = suite->device_.game->getCurrentState()->getStateId();
    ASSERT_LE(stateId, 22);
}

// Test: returnToPreviousApp goes back to Quickdraw
void appSwitchingReturnToPrevious(AppSwitchingTestSuite* suite) {
    // Switch to Signal Echo
    suite->device_.pdn->setActiveApp(StateId(SIGNAL_ECHO_APP_ID));
    suite->tick(1);

    // Get the Signal Echo app and check it's in the Echo state range
    auto* echo = suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID));
    int stateId = echo->getCurrentState()->getStateId();
    ASSERT_GE(stateId, ECHO_INTRO);

    // Return to previous (Quickdraw)
    suite->device_.pdn->returnToPreviousApp();
    suite->tick(1);

    stateId = suite->device_.game->getCurrentState()->getStateId();
    ASSERT_LT(stateId, ECHO_INTRO);
}

#endif // NATIVE_BUILD
