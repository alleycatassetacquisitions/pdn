//
// MiniGame Base Class Tests + Color Profile Tests
//

#pragma once

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "game/minigame.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

// ============================================
// MINIGAME TEST SUITE
// ============================================

class MiniGameTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("test_mg_clock");
        globalLogger_ = new NativeLoggerDriver("test_mg_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);

        device_ = cli::DeviceFactory::createGameDevice(0, "signal-echo");
        game_ = dynamic_cast<SignalEcho*>(device_.game);
    }

    void TearDown() override {
        cli::DeviceFactory::destroyDevice(device_);
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    cli::DeviceInstance device_;
    SignalEcho* game_ = nullptr;
    NativeClockDriver* globalClock_ = nullptr;
    NativeLoggerDriver* globalLogger_ = nullptr;
};

// Test: getGameType and getDisplayName return correct values
void miniGameBaseIdentity(MiniGameTestSuite* suite) {
    ASSERT_EQ(suite->game_->getGameType(), GameType::SIGNAL_ECHO);
    ASSERT_STREQ(suite->game_->getDisplayName(), "SIGNAL ECHO");
}

// Test: Fresh MiniGame has outcome IN_PROGRESS, isGameComplete false
void miniGameBaseOutcomeDefault(MiniGameTestSuite* suite) {
    // Reset to ensure clean state
    suite->game_->resetGame();

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::IN_PROGRESS);
    ASSERT_EQ(outcome.score, 0);
    ASSERT_FALSE(outcome.hardMode);
    ASSERT_FALSE(suite->game_->isGameComplete());
}

// ============================================
// COLOR PROFILE TEST SUITE
// ============================================

class ColorProfileTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("test_cp_clock");
        globalLogger_ = new NativeLoggerDriver("test_cp_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    NativeClockDriver* globalClock_ = nullptr;
    NativeLoggerDriver* globalLogger_ = nullptr;
};

// Test: Signal Echo palette has correct magenta colors
void colorProfileSignalEchoPaletteValues(ColorProfileTestSuite* suite) {
    // Verify the primary palette
    ASSERT_EQ(signalEchoColors[0].red, 255);
    ASSERT_EQ(signalEchoColors[0].green, 0);
    ASSERT_EQ(signalEchoColors[0].blue, 255);

    ASSERT_EQ(signalEchoColors[3].red, 180);
    ASSERT_EQ(signalEchoColors[3].green, 0);
    ASSERT_EQ(signalEchoColors[3].blue, 255);
}

// Test: Idle palette has 8 entries
void colorProfileIdlePaletteSize(ColorProfileTestSuite* suite) {
    // signalEchoIdleColors should have 8 entries
    ASSERT_EQ(sizeof(signalEchoIdleColors) / sizeof(LEDColor), 8u);
    ASSERT_EQ(sizeof(defaultProfileIdleColors) / sizeof(LEDColor), 8u);
}

// Test: Default palette has warm yellow/white values
void colorProfileDefaultPaletteValues(ColorProfileTestSuite* suite) {
    ASSERT_EQ(defaultProfileColors[0].red, 255);
    ASSERT_EQ(defaultProfileColors[0].green, 255);
    ASSERT_EQ(defaultProfileColors[0].blue, 100);

    ASSERT_EQ(defaultProfileColors[2].red, 255);
    ASSERT_EQ(defaultProfileColors[2].green, 255);
    ASSERT_EQ(defaultProfileColors[2].blue, 255);
}

// Test: SIGNAL_ECHO_IDLE_STATE has non-zero brightness
void colorProfileIdleStateHasBrightness(ColorProfileTestSuite* suite) {
    ASSERT_GT(SIGNAL_ECHO_IDLE_STATE.leftLights[0].brightness, 0);
    ASSERT_GT(SIGNAL_ECHO_IDLE_STATE.rightLights[0].brightness, 0);
}

// Test: Win state uses rainbow colors
void colorProfileWinStateIsRainbow(ColorProfileTestSuite* suite) {
    // First LED should be red
    ASSERT_EQ(SIGNAL_ECHO_WIN_STATE.leftLights[0].color.red, 255);
    ASSERT_EQ(SIGNAL_ECHO_WIN_STATE.leftLights[0].color.green, 0);
    ASSERT_EQ(SIGNAL_ECHO_WIN_STATE.leftLights[0].color.blue, 0);

    // Last LED should be magenta
    ASSERT_EQ(SIGNAL_ECHO_WIN_STATE.leftLights[8].color.red, 255);
    ASSERT_EQ(SIGNAL_ECHO_WIN_STATE.leftLights[8].color.green, 0);
    ASSERT_EQ(SIGNAL_ECHO_WIN_STATE.leftLights[8].color.blue, 255);

    // All LEDs should be at full brightness
    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(SIGNAL_ECHO_WIN_STATE.leftLights[i].brightness, 255);
    }
}

// Test: Lose state uses red colors
void colorProfileLoseStateIsRed(ColorProfileTestSuite* suite) {
    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(SIGNAL_ECHO_LOSE_STATE.leftLights[i].color.red, 255);
        ASSERT_EQ(SIGNAL_ECHO_LOSE_STATE.leftLights[i].color.green, 0);
        ASSERT_EQ(SIGNAL_ECHO_LOSE_STATE.leftLights[i].color.blue, 0);
    }
}

// Test: Error state is all red at full brightness
void colorProfileErrorStateIsRed(ColorProfileTestSuite* suite) {
    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(SIGNAL_ECHO_ERROR_STATE.leftLights[i].color.red, 255);
        ASSERT_EQ(SIGNAL_ECHO_ERROR_STATE.leftLights[i].brightness, 255);
    }
}

// Test: Correct state is all green at full brightness
void colorProfileCorrectStateIsGreen(ColorProfileTestSuite* suite) {
    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(SIGNAL_ECHO_CORRECT_STATE.leftLights[i].color.green, 255);
        ASSERT_EQ(SIGNAL_ECHO_CORRECT_STATE.leftLights[i].color.red, 0);
        ASSERT_EQ(SIGNAL_ECHO_CORRECT_STATE.leftLights[i].brightness, 255);
    }
}
