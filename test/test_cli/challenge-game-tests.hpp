//
// Challenge Game Tests — NPC state machine and state behavior
//

#pragma once

#include <gtest/gtest.h>
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-display-driver.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/drivers/native/native-haptics-driver.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device/pdn.hpp"
#include "game/challenge-game.hpp"
#include "game/challenge-states.hpp"
#include "game/challenge-result-manager.hpp"
#include "utils/simple-timer.hpp"

// ============================================
// CHALLENGE GAME TEST SUITE
// ============================================

class ChallengeGameTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("cg_test_clock");
        globalLogger_ = new NativeLoggerDriver("cg_test_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);

        std::string suffix = "_cg_test";
        displayDriver_ = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        primaryButton_ = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
        secondaryButton_ = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        lightDriver_ = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        hapticsDriver_ = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
        serialOut_ = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        serialIn_ = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        httpClient_ = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        peerComms_ = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        storageDriver_ = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);

        DriverConfig config = {
            {DISPLAY_DRIVER_NAME, displayDriver_},
            {PRIMARY_BUTTON_DRIVER_NAME, primaryButton_},
            {SECONDARY_BUTTON_DRIVER_NAME, secondaryButton_},
            {LIGHT_DRIVER_NAME, lightDriver_},
            {HAPTICS_DRIVER_NAME, hapticsDriver_},
            {SERIAL_OUT_DRIVER_NAME, serialOut_},
            {SERIAL_IN_DRIVER_NAME, serialIn_},
            {HTTP_CLIENT_DRIVER_NAME, httpClient_},
            {PEER_COMMS_DRIVER_NAME, peerComms_},
            {PLATFORM_CLOCK_DRIVER_NAME, globalClock_},
            {LOGGER_DRIVER_NAME, globalLogger_},
            {STORAGE_DRIVER_NAME, storageDriver_},
        };

        pdn_ = PDN::createPDN(config);
        pdn_->begin();
        pdn_->setActiveComms(SerialIdentifier::OUTPUT_JACK);

        game_ = new ChallengeGame(pdn_, GameType::SIGNAL_ECHO, KonamiButton::START);
        game_->initialize();
    }

    void TearDown() override {
        delete game_;
        // Note: drivers are owned by DriverManager via PDN, so they're
        // deleted when PDN is deleted. Don't double-delete globalClock_/globalLogger_.
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete pdn_;
    }

    // Helper: run N loop cycles
    void runLoops(int n) {
        for (int i = 0; i < n; i++) {
            pdn_->loop();
            game_->loop();
        }
    }

    NativeClockDriver* globalClock_;
    NativeLoggerDriver* globalLogger_;
    NativeDisplayDriver* displayDriver_;
    NativeButtonDriver* primaryButton_;
    NativeButtonDriver* secondaryButton_;
    NativeLightStripDriver* lightDriver_;
    NativeHapticsDriver* hapticsDriver_;
    NativeSerialDriver* serialOut_;
    NativeSerialDriver* serialIn_;
    NativeHttpClientDriver* httpClient_;
    NativePeerCommsDriver* peerComms_;
    NativePrefsDriver* storageDriver_;
    PDN* pdn_;
    ChallengeGame* game_;
};

// Test: NpcIdle sends "cdev:" message periodically via serial
void npcIdleBroadcastsCdev(ChallengeGameTestSuite* suite) {
    // Game starts in NpcIdle (state 0)
    State* state = suite->game_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), NPC_IDLE);

    // Advance clock past broadcast interval (500ms) and run loops
    suite->globalClock_->advance(600);
    suite->runLoops(5);

    // Check serial output for "cdev:" message
    const auto& sentHistory = suite->serialOut_->getSentHistory();
    bool foundCdev = false;
    for (const auto& msg : sentHistory) {
        if (msg.rfind("cdev:", 0) == 0) {
            foundCdev = true;
            // Verify format: "cdev:7:6" for Signal Echo
            ASSERT_EQ(msg, "cdev:7:6");
            break;
        }
    }
    ASSERT_TRUE(foundCdev) << "NpcIdle should broadcast cdev: messages";
}

// Test: NpcIdle renders game name + Konami button to display
void npcIdleDisplaysGameName(ChallengeGameTestSuite* suite) {
    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), NPC_IDLE);

    // Check display text history for game name
    const auto& textHistory = suite->displayDriver_->getTextHistory();
    bool foundGameName = false;
    for (const auto& text : textHistory) {
        if (text.find("SIGNAL ECHO") != std::string::npos) {
            foundGameName = true;
            break;
        }
    }
    ASSERT_TRUE(foundGameName) << "NpcIdle should display the game name";
}

// Test: Receiving "smac<MAC>" transitions NpcIdle -> NpcHandshake
void npcIdleTransitionsOnMac(ChallengeGameTestSuite* suite) {
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), NPC_IDLE);

    // Inject a MAC address message via serial
    suite->serialOut_->injectInput("smac02:00:00:00:00:99");

    suite->runLoops(5);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), NPC_HANDSHAKE);
}

// Test: NpcHandshake sends "cack" via serial
void npcHandshakeSendsCack(ChallengeGameTestSuite* suite) {
    // Transition to NpcHandshake first
    suite->serialOut_->injectInput("smac02:00:00:00:00:99");
    suite->runLoops(5);
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), NPC_HANDSHAKE);

    // Inject MAC again (handshake reads it)
    suite->serialOut_->injectInput("smac02:00:00:00:00:99");
    suite->runLoops(5);

    // Check for "cack" in serial output
    const auto& sentHistory = suite->serialOut_->getSentHistory();
    bool foundCack = false;
    for (const auto& msg : sentHistory) {
        if (msg == "cack") {
            foundCack = true;
            break;
        }
    }
    ASSERT_TRUE(foundCack) << "NpcHandshake should send cack after receiving MAC";
}

// Test: NpcReceiveResult displays "PLAYER WON" or "PLAYER LOST"
void npcReceiveResultShowsOutcome(ChallengeGameTestSuite* suite) {
    // Manually set last result and skip to NpcReceiveResult
    suite->game_->setLastResult(true);
    suite->game_->setLastScore(300);
    suite->game_->skipToState(NPC_RECEIVE_RESULT);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), NPC_RECEIVE_RESULT);

    const auto& textHistory = suite->displayDriver_->getTextHistory();
    bool foundWon = false;
    for (const auto& text : textHistory) {
        if (text.find("PLAYER WON") != std::string::npos) {
            foundWon = true;
            break;
        }
    }
    ASSERT_TRUE(foundWon) << "NpcReceiveResult should display PLAYER WON";
}

// Test: NpcReceiveResult caches result in NVS
void npcReceiveResultCachesResult(ChallengeGameTestSuite* suite) {
    suite->game_->setLastResult(true);
    suite->game_->setLastScore(500);
    suite->game_->skipToState(NPC_RECEIVE_RESULT);

    // Check NVS for cached result
    uint8_t count = suite->storageDriver_->readUChar(NPC_RESULT_COUNT_KEY, 0);
    ASSERT_GE(count, 1u) << "Result should be cached in NVS";

    std::string result = suite->storageDriver_->read(std::string(NPC_RESULT_KEY) + "0", "");
    ASSERT_FALSE(result.empty());
    // Format: "7:1:500" (gameType:won:score)
    ASSERT_EQ(result, "7:1:500");
}

// Test: NpcReceiveResult transitions back to NpcIdle after delay
void npcReceiveResultTransitions(ChallengeGameTestSuite* suite) {
    suite->game_->setLastResult(false);
    suite->game_->setLastScore(0);
    suite->game_->skipToState(NPC_RECEIVE_RESULT);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), NPC_RECEIVE_RESULT);

    // Advance time past display duration (3000ms)
    // SimpleTimer uses the platform clock — advance it
    suite->globalClock_->advance(3500);
    suite->runLoops(5);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), NPC_IDLE);
}

// Test: NpcIdle LED animation is active
void npcLedAnimationPlays(ChallengeGameTestSuite* suite) {
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), NPC_IDLE);

    // LightManager should have an active animation
    ASSERT_TRUE(suite->pdn_->getLightManager()->isAnimating());
}
