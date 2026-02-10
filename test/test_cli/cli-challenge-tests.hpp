//
// CLI Challenge Tests — factory and spawn command
//

#pragma once

#include <gtest/gtest.h>
#include <set>
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-commands.hpp"
#include "game/challenge-game.hpp"
#include "game/challenge-states.hpp"
#include "utils/simple-timer.hpp"

// ============================================
// CLI CHALLENGE TEST SUITE
// ============================================

class CliChallengeTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("cli_ch_test_clock");
        globalLogger_ = new NativeLoggerDriver("cli_ch_test_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);
    }

    void TearDown() override {
        for (auto& dev : devices_) {
            cli::DeviceFactory::destroyDevice(dev);
        }
        devices_.clear();
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    std::vector<cli::DeviceInstance> devices_;
    NativeClockDriver* globalClock_;
    NativeLoggerDriver* globalLogger_;
};

// Test: createChallengeDevice() produces DeviceInstance with CHALLENGE type
void cliFactoryCreatesChallenge(CliChallengeTestSuite* suite) {
    auto device = cli::DeviceFactory::createChallengeDevice(0, GameType::SIGNAL_ECHO);
    suite->devices_.push_back(device);

    ASSERT_EQ(device.deviceType, DeviceType::CHALLENGE);
    ASSERT_EQ(device.gameType, GameType::SIGNAL_ECHO);
    ASSERT_NE(device.game, nullptr);
    ASSERT_NE(device.pdn, nullptr);
    ASSERT_EQ(device.player, nullptr);  // NPC has no player
}

// Test: Created NPC device runs ChallengeGame (not Quickdraw)
void cliFactoryChallengeHasCorrectGame(CliChallengeTestSuite* suite) {
    auto device = cli::DeviceFactory::createChallengeDevice(0, GameType::SIGNAL_ECHO);
    suite->devices_.push_back(device);

    // Verify it starts in NpcIdle (state 0 of ChallengeGame)
    State* state = device.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), NPC_IDLE);

    // Verify we can dynamic_cast to ChallengeGame
    ChallengeGame* cg = dynamic_cast<ChallengeGame*>(device.game);
    ASSERT_NE(cg, nullptr);
    ASSERT_EQ(cg->getGameType(), GameType::SIGNAL_ECHO);
    ASSERT_EQ(cg->getReward(), KonamiButton::START);
}

// Test: No two devices in a multi-device test share the same pairing code
void uniqueCodesInTestFixture(CliChallengeTestSuite* suite) {
    // Create a mix of player and NPC devices
    suite->devices_.push_back(cli::DeviceFactory::createDevice(0, true));   // Hunter 0010
    suite->devices_.push_back(cli::DeviceFactory::createDevice(1, false));  // Bounty 0011
    suite->devices_.push_back(cli::DeviceFactory::createChallengeDevice(2, GameType::SIGNAL_ECHO));

    std::set<std::string> ids;
    for (const auto& dev : suite->devices_) {
        ASSERT_TRUE(ids.insert(dev.deviceId).second)
            << "Duplicate device ID: " << dev.deviceId;
    }
    ASSERT_EQ(ids.size(), 3u);
}
