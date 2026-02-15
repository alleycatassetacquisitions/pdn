#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "utils/simple-timer.hpp"
#include "../test-constants.hpp"

using namespace cli;

class FdnGameTestSuite : public testing::Test {
public:
    void SetUp() override {
        fdn_ = DeviceFactory::createFdnDevice(0, GameType::SIGNAL_ECHO);
        SimpleTimer::setPlatformClock(fdn_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(fdn_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            fdn_.pdn->loop();
        }
    }

    DeviceInstance fdn_;
};

// Test: FDN device creates with correct type
void fdnGameDeviceType(FdnGameTestSuite* suite) {
    ASSERT_EQ(suite->fdn_.deviceType, DeviceType::FDN);
    ASSERT_EQ(suite->fdn_.gameType, GameType::SIGNAL_ECHO);
}

// Test: FDN device starts in NpcIdle state
void fdnGameStartsInNpcIdle(FdnGameTestSuite* suite) {
    State* state = suite->fdn_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), 0);  // NPC_IDLE
}

// Test: NpcIdle broadcasts FDN identification
void fdnGameNpcIdleBroadcasts(FdnGameTestSuite* suite) {
    // Advance clock past broadcast interval
    suite->fdn_.clockDriver->advance(600);
    suite->tick(2);

    // Check serial output for FDN broadcast
    auto& history = suite->fdn_.serialOutDriver->getSentHistory();
    bool foundBroadcast = false;
    for (const auto& msg : history) {
        if (msg.find("fdn:") == 0) {
            foundBroadcast = true;
            // Verify it contains the game type (7 = SIGNAL_ECHO)
            ASSERT_NE(msg.find("7"), std::string::npos);
            break;
        }
    }
    ASSERT_TRUE(foundBroadcast) << "NpcIdle should broadcast FDN identification";
}

// Test: NpcIdle transitions to NpcHandshake on receiving MAC address
void fdnGameNpcIdleTransitionsOnMac(FdnGameTestSuite* suite) {
    suite->fdn_.serialOutDriver->injectInput("*smac" + std::string(TestConstants::TEST_MAC_DEFAULT) + "\r");
    suite->tick(3);

    State* state = suite->fdn_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), 1);  // NPC_HANDSHAKE
}

// Test: NpcHandshake sends fack on receiving MAC
void fdnGameHandshakeSendsFack(FdnGameTestSuite* suite) {
    // First get to handshake
    suite->fdn_.serialOutDriver->injectInput("*smac" + std::string(TestConstants::TEST_MAC_DEFAULT) + "\r");
    suite->tick(3);

    // Now inject another MAC in handshake state
    suite->fdn_.serialOutDriver->injectInput("*smac" + std::string(TestConstants::TEST_MAC_NPC_1) + "\r");
    suite->tick(3);

    auto& history = suite->fdn_.serialOutDriver->getSentHistory();
    bool foundFack = false;
    for (const auto& msg : history) {
        if (msg == "fack") {
            foundFack = true;
            break;
        }
    }
    ASSERT_TRUE(foundFack) << "NpcHandshake should send fack after receiving MAC";
}

// Test: NpcHandshake times out and returns to NpcIdle
void fdnGameHandshakeTimeout(FdnGameTestSuite* suite) {
    suite->fdn_.serialOutDriver->injectInput("*smac" + std::string(TestConstants::TEST_MAC_DEFAULT) + "\r");
    suite->tick(3);

    ASSERT_EQ(suite->fdn_.game->getCurrentState()->getStateId(), 1);  // NPC_HANDSHAKE

    // Advance past timeout
    suite->fdn_.clockDriver->advance(11000);
    suite->tick(3);

    ASSERT_EQ(suite->fdn_.game->getCurrentState()->getStateId(), 0);  // NPC_IDLE
}

// Test: FdnGame tracks game type and reward
void fdnGameTracksTypeAndReward(FdnGameTestSuite* suite) {
    FdnGame* fdnGame = static_cast<FdnGame*>(suite->fdn_.game);
    ASSERT_EQ(fdnGame->getGameType(), GameType::SIGNAL_ECHO);
    ASSERT_EQ(fdnGame->getReward(), KonamiButton::START);
}

// Test: FdnGame last result can be set and read
void fdnGameLastResultTracking(FdnGameTestSuite* suite) {
    FdnGame* fdnGame = static_cast<FdnGame*>(suite->fdn_.game);
    ASSERT_FALSE(fdnGame->getLastResult());
    ASSERT_EQ(fdnGame->getLastScore(), 0);

    fdnGame->setLastResult(true);
    fdnGame->setLastScore(850);

    ASSERT_TRUE(fdnGame->getLastResult());
    ASSERT_EQ(fdnGame->getLastScore(), 850);
}

#endif // NATIVE_BUILD
