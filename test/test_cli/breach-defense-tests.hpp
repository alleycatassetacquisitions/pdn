#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-states.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// BREACH DEFENSE STUB TEST SUITE
// ============================================

class BreachDefenseStubTestSuite : public testing::Test {
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

    BreachDefense* getBreachDefense() {
        return static_cast<BreachDefense*>(
            player_.pdn->getApp(StateId(BREACH_DEFENSE_APP_ID)));
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
void breachDefenseStubIntroState(BreachDefenseStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "breach-defense");
    SimpleTimer::setPlatformClock(device.clockDriver);

    State* state = device.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), BREACH_INTRO);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Intro auto-transitions to Win after 2 seconds (stub behavior).
 * The stub has no real gameplay — intro -> win is the entire flow.
 */
void breachDefenseStubAutoWin(BreachDefenseStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "breach-defense");
    SimpleTimer::setPlatformClock(device.clockDriver);

    ASSERT_EQ(device.game->getCurrentState()->getStateId(), BREACH_INTRO);

    // Advance past 2s intro timer
    for (int i = 0; i < 25; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }

    ASSERT_EQ(device.game->getCurrentState()->getStateId(), BREACH_WIN);

    // Verify outcome was set
    auto* bd = static_cast<BreachDefense*>(device.game);
    ASSERT_EQ(bd->getOutcome().result, MiniGameResult::WON);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: In managed mode, Win state calls returnToPreviousApp.
 * Launches Breach Defense via FDN handshake and plays through to win.
 * After win timer expires, should return to Quickdraw (FdnComplete).
 */
void breachDefenseStubManagedModeReturns(BreachDefenseStubTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN handshake for Breach Defense (GameType 6, KonamiButton A=5)
    suite->player_.serialOutDriver->injectInput("*fdn:6:5\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Should be in Breach Defense Intro now
    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);
    ASSERT_TRUE(bd->getConfig().managedMode);

    // Advance past intro timer (2s)
    suite->tickWithTime(25, 100);

    // Should be in Breach Defense Win
    ASSERT_EQ(bd->getCurrentState()->getStateId(), BREACH_WIN);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

/*
 * Test: In standalone mode, Win state loops back to Intro.
 * The default standalone config has managedMode = false.
 */
void breachDefenseStubStandaloneLoops(BreachDefenseStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "breach-defense");
    SimpleTimer::setPlatformClock(device.clockDriver);

    // Advance past intro (2s) -> win state
    for (int i = 0; i < 25; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }
    ASSERT_EQ(device.game->getCurrentState()->getStateId(), BREACH_WIN);

    // Advance past win timer (3s) -> should loop to intro
    for (int i = 0; i < 35; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }
    ASSERT_EQ(device.game->getCurrentState()->getStateId(), BREACH_INTRO);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Full FDN flow — detected -> handshake -> game launch -> win -> FdnComplete.
 * This exercises the routing in fdn-detected-state.cpp and the managed mode return.
 */
void breachDefenseStubFdnLaunch(BreachDefenseStubTestSuite* suite) {
    suite->advanceToIdle();

    // Inject FDN broadcast
    suite->player_.serialOutDriver->injectInput("*fdn:6:5\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Now in Breach Defense
    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);
    ASSERT_EQ(bd->getCurrentState()->getStateId(), BREACH_INTRO);

    // Play through stub (intro 2s + win 3s)
    suite->tickWithTime(25, 100);  // Past intro
    ASSERT_EQ(bd->getCurrentState()->getStateId(), BREACH_WIN);
    ASSERT_EQ(bd->getOutcome().result, MiniGameResult::WON);

    suite->tickWithTime(35, 100);  // Past win timer

    // Back in Quickdraw
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

/*
 * Test: App ID is correctly registered in getAppIdForGame().
 */
void breachDefenseStubAppIdRegistered(BreachDefenseStubTestSuite* suite) {
    ASSERT_EQ(getAppIdForGame(GameType::BREACH_DEFENSE), BREACH_DEFENSE_APP_ID);
    ASSERT_EQ(BREACH_DEFENSE_APP_ID, 8);
}

/*
 * Test: State names resolve correctly for CLI display.
 */
void breachDefenseStubStateNames(BreachDefenseStubTestSuite* suite) {
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_INTRO), "BreachDefenseIntro");
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_WIN), "BreachDefenseWin");
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_LOSE), "BreachDefenseLose");

    // Verify getStateName routes correctly
    ASSERT_STREQ(getStateName(BREACH_INTRO), "BreachDefenseIntro");
    ASSERT_STREQ(getStateName(BREACH_WIN), "BreachDefenseWin");
}

#endif // NATIVE_BUILD
