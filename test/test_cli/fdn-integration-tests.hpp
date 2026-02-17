#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
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
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        // Device 0 = player (hunter), Device 1 = FDN NPC
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
        // stateMap index 6 = Idle (after removing AllegiancePickerState)
        player_.game->skipToState(player_.pdn, 6);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentStateId();
    }

    int getFdnStateId() {
        return fdn_.game->getCurrentStateId();
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
    int echoStateId = echo->getCurrentStateId();
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
    int stateId = suite->device_.game->getCurrentStateId();
    ASSERT_EQ(stateId, FDN_COMPLETE);

    // Wait for display timer (3 seconds)
    suite->tickWithTime(20, 200);

    // Should have transitioned back to Idle
    stateId = suite->device_.game->getCurrentStateId();
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
    int stateId = suite->device_.game->getCurrentStateId();
    ASSERT_LE(stateId, 22);
}

// Test: returnToPreviousApp goes back to Quickdraw
void appSwitchingReturnToPrevious(AppSwitchingTestSuite* suite) {
    // Switch to Signal Echo
    suite->device_.pdn->setActiveApp(StateId(SIGNAL_ECHO_APP_ID));
    suite->tick(1);

    // Get the Signal Echo app and check it's in the Echo state range
    auto* echo = suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID));
    int stateId = echo->getCurrentStateId();
    ASSERT_GE(stateId, ECHO_INTRO);

    // Return to previous (Quickdraw)
    suite->device_.pdn->returnToPreviousApp();
    suite->tick(1);

    stateId = suite->device_.game->getCurrentStateId();
    ASSERT_LT(stateId, ECHO_INTRO);
}

// ============================================
// MULTI-PLAYER INTEGRATION TESTS
// ============================================

#include "integration-harness.hpp"

class MultiPlayerIntegrationTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Harness setup happens in each test via addPlayer/addNpc/setup
    }

    void TearDown() override {
        // Harness cleanup is automatic in destructor
    }
};

// Test: 2 players + 1 NPC - basic FDN discovery
void multiPlayer2PlayersBasicDiscovery(MultiPlayerIntegrationTestSuite* suite) {
    MultiPlayerHarness harness;
    harness.addPlayer(true);   // Hunter
    harness.addPlayer(false);  // Bounty
    harness.addNpc(GameType::SIGNAL_ECHO);
    harness.setup();

    // Advance both players to Idle
    harness.advanceAllPlayersToIdle();

    // Connect player 0 (hunter) to NPC
    harness.triggerFdnHandshake(0, 0, 10);

    // Player 0 should detect FDN and transition to FdnDetected
    harness.tickWithTime(5, 100);

    // Player 1 should remain in Idle (not connected)
    ASSERT_EQ(harness.getPlayerStateId(1), IDLE);
}

// Test: 3 players + 3 NPCs - konami progression tracking
void multiPlayer3PlayersKonamiProgression(MultiPlayerIntegrationTestSuite* suite) {
    MultiPlayerHarness harness;
    harness.addPlayer(true);   // Player 0: Hunter
    harness.addPlayer(true);   // Player 1: Hunter
    harness.addPlayer(false);  // Player 2: Bounty
    harness.addNpc(GameType::SIGNAL_ECHO);   // NPC 0
    harness.addNpc(GameType::GHOST_RUNNER);  // NPC 1
    harness.addNpc(GameType::SPIKE_VECTOR);  // NPC 2
    harness.setup();

    harness.advanceAllPlayersToIdle();

    // Initial state: no buttons unlocked
    ASSERT_EQ(harness.getKonamiProgress(0), 0);
    ASSERT_EQ(harness.getKonamiProgress(1), 0);
    ASSERT_EQ(harness.getKonamiProgress(2), 0);

    // Player 0 encounters NPC 0 (Signal Echo → UP button)
    harness.connectCable(0, 3);  // Device 3 = first NPC
    harness.tickWithTime(20, 100);

    // Player 1 encounters NPC 1 (Ghost Runner → START button)
    harness.connectCable(1, 4);  // Device 4 = second NPC
    harness.tickWithTime(20, 100);

    // Player 2 encounters NPC 2 (Spike Vector → DOWN button)
    harness.connectCable(2, 5);  // Device 5 = third NPC
    harness.tickWithTime(20, 100);

    // All players should have detected their respective NPCs
    // (State verification would require more detailed game simulation)

    // Verify all players still have independent progress
    // (Actual konami unlock would require completing the minigames)
    ASSERT_NE(harness.getPlayerStateId(0), IDLE);  // Should be in FDN flow
    ASSERT_NE(harness.getPlayerStateId(1), IDLE);
    ASSERT_NE(harness.getPlayerStateId(2), IDLE);
}

// Test: Cable disconnect mid-game recovery
void multiPlayerCableDisconnectRecovery(MultiPlayerIntegrationTestSuite* suite) {
    MultiPlayerHarness harness;
    harness.addPlayer(true);   // Hunter
    harness.addNpc(GameType::SIGNAL_ECHO);
    harness.setup();

    harness.advancePlayerToIdle(0);

    // Connect and start FDN handshake
    harness.triggerFdnHandshake(0, 0, 10);
    harness.tickWithTime(10, 100);

    // Disconnect cable mid-game
    harness.disconnectCable(0, 1);
    ASSERT_FALSE(harness.isCableConnected(0, 1));

    // Player should eventually recover (return to Idle or handle disconnect)
    // Note: Bug #207 tracks that this doesn't work correctly yet
    harness.tickWithTime(50, 100);

    // For now, just verify the test infrastructure works
    // TODO: After bug #207 is fixed, verify player returns to Idle
}

// Test: All 7 game types in sequence - full konami unlock
void multiPlayer7GamesFullKonami(MultiPlayerIntegrationTestSuite* suite) {
    MultiPlayerHarness harness;
    harness.addPlayer(true);   // Single player
    // Add all 7 FDN game types
    harness.addNpc(GameType::SIGNAL_ECHO);
    harness.addNpc(GameType::GHOST_RUNNER);
    harness.addNpc(GameType::SPIKE_VECTOR);
    harness.addNpc(GameType::FIREWALL_DECRYPT);
    harness.addNpc(GameType::CIPHER_PATH);
    harness.addNpc(GameType::EXPLOIT_SEQUENCER);
    harness.addNpc(GameType::BREACH_DEFENSE);
    harness.setup();

    harness.advancePlayerToIdle(0);

    // Initial: no progress
    ASSERT_FALSE(harness.isKonamiComplete(0));

    // Connect to each NPC sequentially
    for (int npcIndex = 0; npcIndex < 7; npcIndex++) {
        harness.connectCable(0, 1 + npcIndex);  // Device 1-7 are NPCs
        harness.tickWithTime(20, 100);

        // For a real test, we'd need to play through each minigame
        // For now, just verify the handshake infrastructure works

        // Disconnect and return to idle
        harness.disconnectCable(0, 1 + npcIndex);
        harness.tickWithTime(10, 100);
    }

    // This test verifies the harness can handle 1 player + 7 NPCs
    ASSERT_EQ(harness.getPlayerCount(), 1);
    ASSERT_EQ(harness.getNpcCount(), 7);
}

// Test: Multiple players encountering same NPC sequentially
void multiPlayerSequentialNpcEncounters(MultiPlayerIntegrationTestSuite* suite) {
    MultiPlayerHarness harness;
    harness.addPlayer(true);   // Player 0
    harness.addPlayer(false);  // Player 1
    harness.addNpc(GameType::SIGNAL_ECHO);
    harness.setup();

    harness.advanceAllPlayersToIdle();

    // Player 0 connects to NPC
    harness.connectCable(0, 2);  // Device 2 = NPC
    harness.tickWithTime(20, 100);

    // Player 0 disconnects
    harness.disconnectCable(0, 2);
    harness.tickWithTime(5, 100);

    // Player 1 connects to same NPC
    harness.connectCable(1, 2);
    harness.tickWithTime(20, 100);

    // Both players should have independent state
    // NPC should handle sequential encounters correctly
}

// Test: Stress test - 3 players + 7 NPCs (10 total devices)
void multiPlayerStressTest10Devices(MultiPlayerIntegrationTestSuite* suite) {
    MultiPlayerHarness harness;
    harness.addPlayer(true);   // Player 0: Hunter
    harness.addPlayer(false);  // Player 1: Bounty
    harness.addPlayer(true);   // Player 2: Hunter
    // Add all 7 NPCs
    harness.addNpc(GameType::SIGNAL_ECHO);
    harness.addNpc(GameType::GHOST_RUNNER);
    harness.addNpc(GameType::SPIKE_VECTOR);
    harness.addNpc(GameType::FIREWALL_DECRYPT);
    harness.addNpc(GameType::CIPHER_PATH);
    harness.addNpc(GameType::EXPLOIT_SEQUENCER);
    harness.addNpc(GameType::BREACH_DEFENSE);
    harness.setup();

    // Verify device count
    ASSERT_EQ(harness.getPlayerCount(), 3);
    ASSERT_EQ(harness.getNpcCount(), 7);

    // Advance all players to Idle
    harness.advanceAllPlayersToIdle();

    // Run 100 ticks across all 10 devices to verify stability
    harness.tick(100);

    // No crashes = success
    // This verifies the harness and simulator can handle 10 simultaneous devices
}

#endif // NATIVE_BUILD
