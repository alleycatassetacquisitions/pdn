#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// CIPHER PATH STUB TEST SUITE
// ============================================

class CipherPathStubTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            player_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            player_.pdn->loop();
        }
    }

    void advanceToIdle() {
        player_.game->skipToState(player_.pdn, 7);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentState()->getStateId();
    }

    CipherPath* getCipherPath() {
        return static_cast<CipherPath*>(
            player_.pdn->getApp(StateId(CIPHER_PATH_APP_ID)));
    }

    DeviceInstance player_;
};

// ============================================
// STUB LIFECYCLE TESTS
// ============================================

/*
 * Test: Game starts in Intro state when launched standalone.
 * Creates a standalone game device and verifies the initial state.
 */
void cipherPathStubIntroState(CipherPathStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "cipher-path");
    SimpleTimer::setPlatformClock(device.clockDriver);

    State* state = device.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), CIPHER_INTRO);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Intro auto-transitions to Win after 2 seconds (stub behavior).
 * The stub has no real gameplay — intro -> win is the entire flow.
 */
void cipherPathStubAutoWin(CipherPathStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "cipher-path");
    SimpleTimer::setPlatformClock(device.clockDriver);

    ASSERT_EQ(device.game->getCurrentState()->getStateId(), CIPHER_INTRO);

    // Advance past 2s intro timer
    for (int i = 0; i < 25; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }

    ASSERT_EQ(device.game->getCurrentState()->getStateId(), CIPHER_WIN);

    // Verify outcome was set
    auto* cp = static_cast<CipherPath*>(device.game);
    ASSERT_EQ(cp->getOutcome().result, MiniGameResult::WON);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: In managed mode, Win state calls returnToPreviousApp.
 * Launches Cipher Path via FDN handshake and plays through to win.
 * After win timer expires, should return to Quickdraw (FdnComplete).
 */
void cipherPathStubManagedModeReturns(CipherPathStubTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN handshake for Cipher Path (GameType 4, KonamiButton RIGHT=3)
    suite->player_.serialOutDriver->injectInput("*fdn:4:3\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Should be in Cipher Path Intro now
    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);
    ASSERT_TRUE(cp->getConfig().managedMode);

    // Advance past intro timer (2s)
    suite->tickWithTime(25, 100);

    // Should be in Cipher Path Win
    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_WIN);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

/*
 * Test: In standalone mode, Win state loops back to Intro.
 * The default standalone config has managedMode = false.
 */
void cipherPathStubStandaloneLoops(CipherPathStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "cipher-path");
    SimpleTimer::setPlatformClock(device.clockDriver);

    // Advance past intro (2s) -> win state
    for (int i = 0; i < 25; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }
    ASSERT_EQ(device.game->getCurrentState()->getStateId(), CIPHER_WIN);

    // Advance past win timer (3s) -> should loop to intro
    for (int i = 0; i < 35; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }
    ASSERT_EQ(device.game->getCurrentState()->getStateId(), CIPHER_INTRO);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Full FDN flow — detected -> handshake -> game launch -> win -> FdnComplete.
 * This exercises the routing in fdn-detected-state.cpp and the managed mode return.
 */
void cipherPathStubFdnLaunch(CipherPathStubTestSuite* suite) {
    suite->advanceToIdle();

    // Inject FDN broadcast
    suite->player_.serialOutDriver->injectInput("*fdn:4:3\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Now in Cipher Path
    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);
    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_INTRO);

    // Play through stub (intro 2s + win 3s)
    suite->tickWithTime(25, 100);  // Past intro
    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_WIN);
    ASSERT_EQ(cp->getOutcome().result, MiniGameResult::WON);

    suite->tickWithTime(35, 100);  // Past win timer

    // Back in Quickdraw
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

/*
 * Test: App ID is correctly registered in getAppIdForGame().
 */
void cipherPathStubAppIdRegistered(CipherPathStubTestSuite* suite) {
    ASSERT_EQ(getAppIdForGame(GameType::CIPHER_PATH), CIPHER_PATH_APP_ID);
    ASSERT_EQ(CIPHER_PATH_APP_ID, 6);
}

/*
 * Test: State names resolve correctly for CLI display.
 */
void cipherPathStubStateNames(CipherPathStubTestSuite* suite) {
    ASSERT_STREQ(getCipherPathStateName(CIPHER_INTRO), "CipherPathIntro");
    ASSERT_STREQ(getCipherPathStateName(CIPHER_WIN), "CipherPathWin");
    ASSERT_STREQ(getCipherPathStateName(CIPHER_LOSE), "CipherPathLose");

    // Verify getStateName routes correctly
    ASSERT_STREQ(getStateName(CIPHER_INTRO), "CipherPathIntro");
    ASSERT_STREQ(getStateName(CIPHER_WIN), "CipherPathWin");
}

#endif // NATIVE_BUILD
