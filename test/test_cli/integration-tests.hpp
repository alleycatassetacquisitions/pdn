//
// Integration Tests -- ChallengeDetected + ChallengeComplete states + SM Manager wiring
//

#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include <cstdint>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "state/state-machine-manager.hpp"
#include "game/progress-manager.hpp"
#include "device/device-types.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include "game/test-minigame.hpp"

// ============================================
// INTEGRATION TEST SUITE
// ============================================

class IntegrationTestSuite : public testing::Test {
public:
    cli::DeviceInstance player_;
    NativeClockDriver* globalClock_ = nullptr;
    NativeLoggerDriver* globalLogger_ = nullptr;

    void SetUp() override {
        // Set up global clock and logger (required for SimpleTimer)
        globalClock_ = new NativeClockDriver("integration_test_clock");
        globalLogger_ = new NativeLoggerDriver("integration_test_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);

        // Create player (hunter)
        player_ = cli::DeviceFactory::createDevice(0, true);

        // Run initial loops to get devices past setup states
        for (int i = 0; i < 5; i++) {
            player_.pdn->loop();
            if (player_.smManager) {
                player_.smManager->loop();
            } else {
                player_.game->loop();
            }
        }

        // Skip player to Idle (stateMap index 7)
        player_.game->skipToState(7);
    }

    void TearDown() override {
        cli::SerialCableBroker::getInstance().disconnectDevice(0);
        cli::DeviceFactory::destroyDevice(player_);
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            NativePeerBroker::getInstance().deliverPackets();
            cli::SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            if (player_.smManager) {
                player_.smManager->loop();
            } else {
                player_.game->loop();
            }
        }
    }

    void advance(int ms) {
        globalClock_->advance(ms);
        tick();
    }
};

// Test: ChallengeDetected transitions from Idle when cdev message arrives
void integrationCdevTriggersTransition(IntegrationTestSuite* suite) {
    // Verify we start in Idle (stateId = IDLE = 8)
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), IDLE);

    // Inject a cdev message via serial input (simulates NPC broadcast)
    suite->player_.serialOutDriver->injectInput("*cdev:7:6\r");
    suite->tick(3);

    // Player should now be in ChallengeDetected (stateId 21)
    state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), CHALLENGE_DETECTED)
        << "Player should transition to ChallengeDetected after cdev received";
}

// Test: ChallengeDetected sends MAC address to NPC
void integrationChallengeDetectedSendsMac(IntegrationTestSuite* suite) {
    // Inject cdev message
    suite->player_.serialOutDriver->injectInput("*cdev:7:6\r");
    suite->tick(3);

    // Verify ChallengeDetected is active
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), CHALLENGE_DETECTED);

    // Check that a MAC address message was sent via serial output
    auto& history = suite->player_.serialOutDriver->getSentHistory();
    bool macSent = false;
    for (const auto& msg : history) {
        if (msg.find("smac") != std::string::npos) {
            macSent = true;
            break;
        }
    }
    ASSERT_TRUE(macSent) << "ChallengeDetected should send MAC address via serial";
}

// Test: ChallengeDetected receives cack and loads minigame via SM Manager
void integrationSwapsOnCack(IntegrationTestSuite* suite) {
    // Inject cdev message
    suite->player_.serialOutDriver->injectInput("*cdev:7:6\r");
    suite->tick(3);

    // Verify in ChallengeDetected
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), CHALLENGE_DETECTED);

    // Inject cack from NPC
    suite->player_.serialOutDriver->injectInput("*cack\r");
    suite->tick(3);

    // After cack, the SM Manager should be swapped to the minigame
    ASSERT_TRUE(suite->player_.smManager->isSwapped())
        << "SM Manager should be swapped to minigame after cack";
}

// Test: Timeout returns to Idle when no cack received
void integrationTimeoutToIdle(IntegrationTestSuite* suite) {
    // Inject cdev message to trigger ChallengeDetected
    suite->player_.serialOutDriver->injectInput("*cdev:7:6\r");
    suite->tick(3);

    // Verify in ChallengeDetected
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), CHALLENGE_DETECTED);

    // Advance past timeout (10 seconds) — need multiple ticks for
    // timer update + transition check + commit cycle
    suite->globalClock_->advance(11000);
    suite->tick(5);

    // Should be back in Idle
    state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), IDLE)
        << "Player should return to Idle after ChallengeDetected timeout";
}

// Test: Invalid cdev message causes immediate transition back to Idle
void integrationInvalidCdevToIdle(IntegrationTestSuite* suite) {
    // Inject an invalid cdev message (missing reward field)
    suite->player_.serialOutDriver->injectInput("*cdev:bad\r");
    suite->tick(3);

    // Should transition to ChallengeDetected first, then back to Idle
    // because the parse fails and sets transitionToIdleState = true
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), IDLE)
        << "Invalid cdev message should result in return to Idle";
}

// Test: ChallengeComplete shows win result and transitions to Idle
void integrationChallengeCompleteWinToIdle(IntegrationTestSuite* suite) {
    #ifdef UNIT_TEST
    // Use TestMiniGame to simulate a completed minigame
    auto* miniGame = new TestMiniGame(suite->player_.pdn, MiniGameResult::WON);
    // Resume to ChallengeComplete (stateMap index 22)
    suite->player_.smManager->pauseAndLoad(miniGame, 22);

    // Trigger the test minigame to complete
    miniGame->triggerComplete();

    // Loop until SM Manager auto-resumes to ChallengeComplete
    for (int i = 0; i < 5; i++) {
        suite->player_.smManager->loop();
        if (!suite->player_.smManager->isSwapped()) break;
    }

    // Should be in ChallengeComplete now (stateId 22)
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), CHALLENGE_COMPLETE)
        << "Should be in ChallengeComplete after minigame win";

    // Advance past display timer (3 seconds) — need multiple ticks for
    // timer update + transition check + commit cycle
    suite->globalClock_->advance(4000);
    suite->tick(5);

    // Should transition to Idle
    state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), IDLE)
        << "Should return to Idle after ChallengeComplete display";
    #endif
}

// Test: ChallengeComplete unlocks Konami button on win
void integrationChallengeCompleteUnlocksKonami(IntegrationTestSuite* suite) {
    #ifdef UNIT_TEST
    // Verify player has no Konami progress initially
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);

    auto* miniGame = new TestMiniGame(suite->player_.pdn, MiniGameResult::WON);
    suite->player_.smManager->pauseAndLoad(miniGame, 22);
    miniGame->triggerComplete();

    for (int i = 0; i < 5; i++) {
        suite->player_.smManager->loop();
        if (!suite->player_.smManager->isSwapped()) break;
    }

    // ChallengeComplete should have unlocked the Konami button for SIGNAL_ECHO
    KonamiButton reward = getRewardForGame(GameType::SIGNAL_ECHO);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(reward))
        << "Signal Echo reward button should be unlocked after win";
    #endif
}

// Test: ChallengeComplete does NOT unlock Konami on loss
void integrationNoUnlockOnLoss(IntegrationTestSuite* suite) {
    #ifdef UNIT_TEST
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);

    auto* miniGame = new TestMiniGame(suite->player_.pdn, MiniGameResult::LOST);
    suite->player_.smManager->pauseAndLoad(miniGame, 22);
    miniGame->triggerComplete();

    for (int i = 0; i < 5; i++) {
        suite->player_.smManager->loop();
        if (!suite->player_.smManager->isSwapped()) break;
    }

    // ChallengeComplete should process loss
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), CHALLENGE_COMPLETE);

    // Advance past display
    suite->advance(4000);
    suite->tick(3);

    // Verify NO Konami unlock
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0)
        << "No Konami buttons should be unlocked on loss";
    #endif
}

// Test: Progress is saved to NVS after a win
void integrationSavesProgress(IntegrationTestSuite* suite) {
    #ifdef UNIT_TEST
    auto* miniGame = new TestMiniGame(suite->player_.pdn, MiniGameResult::WON);
    suite->player_.smManager->pauseAndLoad(miniGame, 22);
    miniGame->triggerComplete();

    for (int i = 0; i < 5; i++) {
        suite->player_.smManager->loop();
        if (!suite->player_.smManager->isSwapped()) break;
    }

    // Wait for ChallengeComplete to process and transition to Idle
    suite->globalClock_->advance(4000);
    suite->tick(5);

    // Verify progress was saved to storage
    KonamiButton reward = getRewardForGame(GameType::SIGNAL_ECHO);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(reward));

    // The progress manager should have saved to NVS (uses writeUChar)
    uint8_t storedProgress = suite->player_.storageDriver->readUChar("konami", 0);
    uint8_t expectedBit = static_cast<uint8_t>(1 << static_cast<uint8_t>(reward));
    ASSERT_TRUE((storedProgress & expectedBit) != 0)
        << "Konami progress should be saved to storage";
    #endif
}

// Test: SM Manager wiring is correct for player devices
void integrationSmManagerWired(IntegrationTestSuite* suite) {
    ASSERT_NE(suite->player_.smManager, nullptr)
        << "SM Manager should be created for player devices";
    ASSERT_FALSE(suite->player_.smManager->isSwapped())
        << "SM Manager should not be swapped initially";
    ASSERT_EQ(suite->player_.smManager->getActive(), suite->player_.game)
        << "SM Manager should delegate to Quickdraw by default";
}

// Test: Player pending challenge fields work correctly
void integrationPlayerPendingChallenge(IntegrationTestSuite* suite) {
    ASSERT_FALSE(suite->player_.player->hasPendingChallenge());

    suite->player_.player->setPendingChallenge("cdev:7:6");
    ASSERT_TRUE(suite->player_.player->hasPendingChallenge());
    ASSERT_EQ(suite->player_.player->getPendingCdevMessage(), "cdev:7:6");

    suite->player_.player->clearPendingChallenge();
    ASSERT_FALSE(suite->player_.player->hasPendingChallenge());
    ASSERT_TRUE(suite->player_.player->getPendingCdevMessage().empty());
}

// Test: Normal handshake (heartbeat) still works when no cdev detected
void integrationNormalHandshakeStillWorks(IntegrationTestSuite* suite) {
    // Verify we start in Idle
    State* state = suite->player_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), IDLE);

    // Inject a normal heartbeat message (from another player device)
    suite->player_.serialOutDriver->injectInput("*hb\r");
    suite->tick(3);

    // Should have left Idle and entered the handshake flow.
    // With multiple ticks, the device may progress past HandshakeInitiate
    // into later handshake states (e.g., HunterSendId = 11)
    state = suite->player_.game->getCurrentState();
    int stateId = state->getStateId();
    ASSERT_NE(stateId, IDLE)
        << "Normal heartbeat should trigger transition out of Idle";
    ASSERT_GE(stateId, HANDSHAKE_INITIATE_STATE)
        << "Should be in handshake flow (stateId >= 9)";
    ASSERT_LE(stateId, HUNTER_SEND_ID_STATE)
        << "Should be in handshake flow (stateId <= 11)";
}

#endif // NATIVE_BUILD
