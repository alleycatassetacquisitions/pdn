#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// SPIKE VECTOR STUB TEST SUITE
// ============================================

class SpikeVectorStubTestSuite : public testing::Test {
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

    SpikeVector* getSpikeVector() {
        return static_cast<SpikeVector*>(
            player_.pdn->getApp(StateId(SPIKE_VECTOR_APP_ID)));
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
void spikeVectorStubIntroState(SpikeVectorStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "spike-vector");
    SimpleTimer::setPlatformClock(device.clockDriver);

    State* state = device.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), SPIKE_INTRO);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Intro auto-transitions to Win after 2 seconds (stub behavior).
 * The stub has no real gameplay — intro -> win is the entire flow.
 */
void spikeVectorStubAutoWin(SpikeVectorStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "spike-vector");
    SimpleTimer::setPlatformClock(device.clockDriver);

    ASSERT_EQ(device.game->getCurrentState()->getStateId(), SPIKE_INTRO);

    // Advance past 2s intro timer
    for (int i = 0; i < 25; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }

    ASSERT_EQ(device.game->getCurrentState()->getStateId(), SPIKE_WIN);

    // Verify outcome was set
    auto* sv = static_cast<SpikeVector*>(device.game);
    ASSERT_EQ(sv->getOutcome().result, MiniGameResult::WON);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: In managed mode, Win state calls returnToPreviousApp.
 * Launches Spike Vector via FDN handshake and plays through to win.
 * After win timer expires, should return to Quickdraw (FdnComplete).
 */
void spikeVectorStubManagedModeReturns(SpikeVectorStubTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN handshake for Spike Vector (GameType 2, KonamiButton DOWN=1)
    suite->player_.serialOutDriver->injectInput("*fdn:2:1\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Should be in Spike Vector Intro now
    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);
    ASSERT_TRUE(sv->getConfig().managedMode);

    // Advance past intro timer (2s)
    suite->tickWithTime(25, 100);

    // Should be in Spike Vector Win
    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_WIN);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

/*
 * Test: In standalone mode, Win state loops back to Intro.
 * The default standalone config has managedMode = false.
 */
void spikeVectorStubStandaloneLoops(SpikeVectorStubTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(10, "spike-vector");
    SimpleTimer::setPlatformClock(device.clockDriver);

    // Advance past intro (2s) -> win state
    for (int i = 0; i < 25; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }
    ASSERT_EQ(device.game->getCurrentState()->getStateId(), SPIKE_WIN);

    // Advance past win timer (3s) -> should loop to intro
    for (int i = 0; i < 35; i++) {
        device.clockDriver->advance(100);
        device.pdn->loop();
    }
    ASSERT_EQ(device.game->getCurrentState()->getStateId(), SPIKE_INTRO);

    SimpleTimer::setPlatformClock(suite->player_.clockDriver);
    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Full FDN flow — detected -> handshake -> game launch -> win -> FdnComplete.
 * This exercises the routing in fdn-detected-state.cpp and the managed mode return.
 */
void spikeVectorStubFdnLaunch(SpikeVectorStubTestSuite* suite) {
    suite->advanceToIdle();

    // Inject FDN broadcast
    suite->player_.serialOutDriver->injectInput("*fdn:2:1\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Now in Spike Vector
    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);
    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_INTRO);

    // Play through stub (intro 2s + win 3s)
    suite->tickWithTime(25, 100);  // Past intro
    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_WIN);
    ASSERT_EQ(sv->getOutcome().result, MiniGameResult::WON);

    suite->tickWithTime(35, 100);  // Past win timer

    // Back in Quickdraw
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

/*
 * Test: App ID is correctly registered in getAppIdForGame().
 */
void spikeVectorStubAppIdRegistered(SpikeVectorStubTestSuite* suite) {
    ASSERT_EQ(getAppIdForGame(GameType::SPIKE_VECTOR), SPIKE_VECTOR_APP_ID);
    ASSERT_EQ(SPIKE_VECTOR_APP_ID, 5);
}

/*
 * Test: State names resolve correctly for CLI display.
 */
void spikeVectorStubStateNames(SpikeVectorStubTestSuite* suite) {
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_INTRO), "SpikeVectorIntro");
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_WIN), "SpikeVectorWin");
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_LOSE), "SpikeVectorLose");

    // Verify getStateName routes correctly
    ASSERT_STREQ(getStateName(SPIKE_INTRO), "SpikeVectorIntro");
    ASSERT_STREQ(getStateName(SPIKE_WIN), "SpikeVectorWin");
}

#endif // NATIVE_BUILD
