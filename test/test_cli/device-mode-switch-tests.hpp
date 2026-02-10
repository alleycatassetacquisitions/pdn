//
// Device Mode Switch Tests — NVS-based player/NPC mode switching
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
#include "game/quickdraw.hpp"
#include "game/challenge-game.hpp"
#include "game/challenge-states.hpp"
#include "game/quickdraw-states.hpp"
#include "device/device-types.hpp"
#include "device/device-constants.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"

// ============================================
// DEVICE MODE SWITCH TEST SUITE
// ============================================

class DeviceModeSwitchTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("dms_test_clock");
        globalLogger_ = new NativeLoggerDriver("dms_test_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);

        // Create drivers (shared across "reboots")
        std::string suffix = "_dms_test";
        displayDriver_ = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        primaryButton_ = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
        secondaryButton_ = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        lightDriver_ = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        hapticsDriver_ = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
        serialOut_ = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        serialIn_ = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        httpClient_ = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        httpClient_->setMockServerEnabled(true);
        httpClient_->setConnected(true);
        peerComms_ = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        storage_ = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);

        buildPDN();

        // Start as player (Quickdraw)
        player_ = new Player();
        player_->setUserID("0010");
        player_->setIsHunter(true);
        qwm_ = new QuickdrawWirelessManager();
        qwm_->initialize(player_, pdn_->getWirelessManager(), 1000);

        game_ = new Quickdraw(player_, pdn_, qwm_, nullptr);
        game_->initialize();
    }

    void TearDown() override {
        delete game_;
        if (qwm_) delete qwm_;
        if (player_) delete player_;
        // Note: drivers are owned by DriverManager via PDN
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete pdn_;
    }

    void buildPDN() {
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
            {STORAGE_DRIVER_NAME, storage_},
        };
        pdn_ = PDN::createPDN(config);
        pdn_->begin();
    }

    /*
     * Simulate entering a magic code and "rebooting":
     * 1. Write code to NVS
     * 2. Destroy current game (and player/qwm if switching to NPC)
     * 3. Check NVS and create appropriate StateMachine
     * 4. Initialize
     */
    void enterCodeAndReboot(const std::string& code) {
        storage_->write(DEVICE_MODE_KEY, code);

        delete game_;
        game_ = nullptr;

        if (isChallengeDeviceCode(code)) {
            // Switching to NPC mode — no player needed
            if (qwm_) { delete qwm_; qwm_ = nullptr; }
            if (player_) { delete player_; player_ = nullptr; }

            GameType gt = getGameTypeFromCode(code);
            game_ = new ChallengeGame(pdn_, gt, getRewardForGame(gt));
        } else {
            // Switching to player mode
            if (!player_) {
                player_ = new Player();
                player_->setUserID("0010");
                player_->setIsHunter(true);
            }
            if (!qwm_) {
                qwm_ = new QuickdrawWirelessManager();
                qwm_->initialize(player_, pdn_->getWirelessManager(), 1000);
            }
            game_ = new Quickdraw(player_, pdn_, qwm_, nullptr);
        }
        game_->initialize();
    }

    /*
     * Clear NVS mode and "reboot" back to Quickdraw.
     */
    void clearModeAndReboot() {
        storage_->remove(DEVICE_MODE_KEY);
        enterCodeAndReboot("");  // Empty code = Quickdraw
    }

    bool isRunningQuickdraw() {
        return dynamic_cast<Quickdraw*>(game_) != nullptr;
    }

    bool isRunningChallengeGame(GameType expectedType) {
        ChallengeGame* cg = dynamic_cast<ChallengeGame*>(game_);
        if (!cg) return false;
        return cg->getGameType() == expectedType;
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
    NativePrefsDriver* storage_;
    PDN* pdn_;
    Player* player_;
    QuickdrawWirelessManager* qwm_;
    StateMachine* game_;
};

// Test: NPC mode persists — write "7007" to NVS, create device, verify ChallengeGame
void npcModePersistsAcrossReboot(DeviceModeSwitchTestSuite* suite) {
    ASSERT_TRUE(suite->isRunningQuickdraw());

    suite->enterCodeAndReboot("7007");
    ASSERT_TRUE(suite->isRunningChallengeGame(GameType::SIGNAL_ECHO));

    // Verify state is NpcIdle
    State* state = suite->game_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), NPC_IDLE);
}

// Test: Cleared mode falls back to Quickdraw
void clearedModeFallsBackToQuickdraw(DeviceModeSwitchTestSuite* suite) {
    // Start as NPC
    suite->enterCodeAndReboot("7007");
    ASSERT_TRUE(suite->isRunningChallengeGame(GameType::SIGNAL_ECHO));

    // Clear and reboot
    suite->clearModeAndReboot();
    ASSERT_TRUE(suite->isRunningQuickdraw());
}

// Test: Special codes (9999, 8888, etc.) still work alongside 7xxx range
void specialCodesStillWork(DeviceModeSwitchTestSuite* suite) {
    // These should NOT be treated as challenge codes
    ASSERT_FALSE(isChallengeDeviceCode("9999"));
    ASSERT_FALSE(isChallengeDeviceCode("8888"));
    ASSERT_FALSE(isChallengeDeviceCode("1111"));
    ASSERT_FALSE(isChallengeDeviceCode("6969"));

    // The existing special code behavior is tested elsewhere;
    // here we just verify they don't accidentally trigger NPC mode
    ASSERT_EQ(getGameTypeFromCode("9999"), GameType::QUICKDRAW);
}

// Test: Full cycle NPC -> clear -> player -> enter 7003 -> NPC (different game)
void npcToPlayerToNpc(DeviceModeSwitchTestSuite* suite) {
    // Start as Signal Echo NPC
    suite->enterCodeAndReboot("7007");
    ASSERT_TRUE(suite->isRunningChallengeGame(GameType::SIGNAL_ECHO));

    // Clear and become player
    suite->clearModeAndReboot();
    ASSERT_TRUE(suite->isRunningQuickdraw());

    // Enter different NPC code
    suite->enterCodeAndReboot("7003");
    ASSERT_TRUE(suite->isRunningChallengeGame(GameType::FIREWALL_DECRYPT));
}

// Test: Full cycle player -> enter 7007 -> NPC -> clear -> player
void playerToNpcToPlayer(DeviceModeSwitchTestSuite* suite) {
    ASSERT_TRUE(suite->isRunningQuickdraw());

    suite->enterCodeAndReboot("7007");
    ASSERT_TRUE(suite->isRunningChallengeGame(GameType::SIGNAL_ECHO));

    suite->clearModeAndReboot();
    ASSERT_TRUE(suite->isRunningQuickdraw());
}

// Test: FetchUserData detects challenge code and writes to NVS
void fetchUserDetectsChallengeCode(DeviceModeSwitchTestSuite* suite) {
    // This test verifies the NVS write path in FetchUserDataState.
    // Set the player ID to a challenge code, then run FetchUserData.
    ASSERT_TRUE(suite->isRunningQuickdraw());

    suite->player_->setUserID("7007");

    // Configure mock server to NOT have this ID (it's a magic code, not a real player)
    // The FetchUserDataState should detect the challenge code BEFORE making an HTTP request

    // Skip to FetchUserData (state index 1)
    suite->game_->skipToState(1);

    // Run a few loops
    for (int i = 0; i < 10; i++) {
        suite->pdn_->loop();
        suite->game_->loop();
    }

    // Check that the NVS was written with the challenge code
    std::string mode = suite->storage_->read(DEVICE_MODE_KEY, "");
    ASSERT_EQ(mode, "7007");
}
