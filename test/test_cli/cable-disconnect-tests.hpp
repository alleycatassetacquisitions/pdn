//
// Cable Disconnect Tests - Tests for minigame behavior when cable is disconnected
//

#pragma once

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// CABLE DISCONNECT TEST SUITE
// ============================================

class CableDisconnectTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        // Create player device (hunter) and FDN device (NPC with Ghost Runner)
        player_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::GHOST_RUNNER);
        SimpleTimer::setPlatformClock(player_.clockDriver);

        // Connect devices via cable
        SerialCableBroker::getInstance().connect(0, 1);
    }

    void TearDown() override {
        SerialCableBroker::getInstance().disconnect(0, 1);
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(player_);

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

    DeviceInstance player_;
    DeviceInstance fdn_;
};

// Test: Cable disconnect during minigame intro causes clean abort
void cableDisconnectDuringIntro(CableDisconnectTestSuite* suite) {
    // Start player device
    suite->player_.pdn->begin();
    suite->fdn_.pdn->begin();

    // Advance player to Idle state
    suite->tickWithTime(100, 100);

    // Player should be in Idle state
    int playerState = suite->player_.pdn->getActiveApp()->getCurrentState()->getStateId();
    ASSERT_EQ(playerState, IDLE);

    // Initiate FDN handshake
    suite->tick(50);

    // Player should detect FDN and enter FdnDetected state
    playerState = suite->player_.pdn->getActiveApp()->getCurrentState()->getStateId();
    ASSERT_EQ(playerState, FDN_DETECTED);

    // Complete handshake and launch minigame
    suite->tick(100);

    // Player should now be in Ghost Runner minigame
    int activeAppId = suite->player_.pdn->getActiveAppId().id;
    ASSERT_EQ(activeAppId, GHOST_RUNNER_APP_ID);

    // Disconnect cable during minigame intro
    SerialCableBroker::getInstance().disconnect(0, 1);

    // Continue ticking - minigame should detect disconnect and abort
    suite->tickWithTime(200, 10);

    // After disconnect + ticks, player should return to Idle (not crash/hang)
    playerState = suite->player_.pdn->getActiveApp()->getCurrentState()->getStateId();
    ASSERT_EQ(playerState, IDLE) << "Player should return to Idle after cable disconnect, not hang in minigame";
}

// Test: Cable disconnect during minigame gameplay causes clean abort
void cableDisconnectDuringGameplay(CableDisconnectTestSuite* suite) {
    // Start player device
    suite->player_.pdn->begin();
    suite->fdn_.pdn->begin();

    // Advance player to Idle state
    suite->tickWithTime(100, 100);

    // Player should be in Idle state
    int playerState = suite->player_.pdn->getActiveApp()->getCurrentState()->getStateId();
    ASSERT_EQ(playerState, IDLE);

    // Initiate FDN handshake
    suite->tick(50);

    // Player should detect FDN and enter FdnDetected state
    playerState = suite->player_.pdn->getActiveApp()->getCurrentState()->getStateId();
    ASSERT_EQ(playerState, FDN_DETECTED);

    // Complete handshake and launch minigame
    suite->tick(100);

    // Player should now be in Ghost Runner minigame
    int activeAppId = suite->player_.pdn->getActiveAppId().id;
    ASSERT_EQ(activeAppId, GHOST_RUNNER_APP_ID);

    // Advance through intro to gameplay
    suite->tickWithTime(300, 10);

    // Disconnect cable during gameplay
    SerialCableBroker::getInstance().disconnect(0, 1);

    // Continue ticking - minigame should detect disconnect and abort
    suite->tickWithTime(200, 10);

    // After disconnect + ticks, player should return to Idle (not crash/hang)
    playerState = suite->player_.pdn->getActiveApp()->getCurrentState()->getStateId();
    ASSERT_EQ(playerState, IDLE) << "Player should return to Idle after cable disconnect during gameplay";
}

// Test: Cable reconnect to different NPC after disconnect
void cableReconnectToDifferentNpc(CableDisconnectTestSuite* suite) {
    // Start player device
    suite->player_.pdn->begin();
    suite->fdn_.pdn->begin();

    // Advance player to Idle state
    suite->tickWithTime(100, 100);

    // Initiate FDN handshake
    suite->tick(50);

    // Complete handshake and launch minigame
    suite->tick(100);

    // Player should be in minigame
    int activeAppId = suite->player_.pdn->getActiveAppId().id;
    ASSERT_EQ(activeAppId, GHOST_RUNNER_APP_ID);

    // Disconnect from first NPC
    SerialCableBroker::getInstance().disconnect(0, 1);

    // Create second FDN device (different minigame)
    DeviceInstance fdn2 = DeviceFactory::createFdnDevice(2, GameType::SPIKE_VECTOR);
    fdn2.pdn->begin();

    // Connect to second NPC
    SerialCableBroker::getInstance().connect(0, 2);

    // Tick to process disconnect and new connection
    suite->tickWithTime(300, 10);

    // Player should return to Idle first
    int playerState = suite->player_.pdn->getActiveApp()->getCurrentState()->getStateId();
    ASSERT_EQ(playerState, IDLE) << "Player should return to Idle after disconnect before detecting new NPC";

    // Cleanup
    SerialCableBroker::getInstance().disconnect(0, 2);
    DeviceFactory::destroyDevice(fdn2);
}
