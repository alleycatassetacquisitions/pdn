#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "../test-constants.hpp"

using namespace cli;

class CliFdnTestSuite : public testing::Test {
protected:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        // Fresh test setup
    }
};

// Test: createFdnDevice produces correct device type
void cliFdnCreateDeviceType(CliFdnTestSuite* suite) {
    auto fdn = DeviceFactory::createFdnDevice(0, GameType::SIGNAL_ECHO);
    ASSERT_EQ(fdn.deviceType, DeviceType::FDN);
    ASSERT_EQ(fdn.gameType, GameType::SIGNAL_ECHO);
    ASSERT_TRUE(fdn.isHunter);  // FDN uses output jack
    ASSERT_EQ(fdn.player, nullptr);  // No player for FDN
    DeviceFactory::destroyDevice(fdn);
}

// Test: createFdnDevice generates 7xxx IDs
void cliFdnDeviceIdRange(CliFdnTestSuite* suite) {
    auto fdn0 = DeviceFactory::createFdnDevice(0, GameType::SIGNAL_ECHO);
    auto fdn1 = DeviceFactory::createFdnDevice(1, GameType::GHOST_RUNNER);
    ASSERT_EQ(fdn0.deviceId, TestConstants::TEST_FDN_DEVICE_ID_0);
    ASSERT_EQ(fdn1.deviceId, TestConstants::TEST_FDN_DEVICE_ID_1);
    DeviceFactory::destroyDevice(fdn1);
    DeviceFactory::destroyDevice(fdn0);
}

// Test: FDN device registers with serial broker
void cliFdnRegistersWithBroker(CliFdnTestSuite* suite) {
    auto fdn = DeviceFactory::createFdnDevice(0, GameType::SIGNAL_ECHO);
    auto player = DeviceFactory::createDevice(1, true);

    // Connect FDN to player via cable
    ASSERT_TRUE(SerialCableBroker::getInstance().connect(0, 1));

    DeviceFactory::destroyDevice(player);
    DeviceFactory::destroyDevice(fdn);
}

// Test: FDN idle state displays game color profile
void cliFdnIdleDisplaysColorProfile(CliFdnTestSuite* suite) {
    // Create FDN devices for different games
    auto signalEcho = DeviceFactory::createFdnDevice(0, GameType::SIGNAL_ECHO);
    auto ghostRunner = DeviceFactory::createFdnDevice(1, GameType::GHOST_RUNNER);
    auto spikeVector = DeviceFactory::createFdnDevice(2, GameType::SPIKE_VECTOR);

    // Run a few loops to ensure animation starts
    for (int i = 0; i < 5; i++) {
        signalEcho.pdn->loop();
        ghostRunner.pdn->loop();
        spikeVector.pdn->loop();
    }

    // Verify current animation is IDLE (not TRANSMIT_BREATH)
    ASSERT_EQ(signalEcho.pdn->getLightManager()->getCurrentAnimation(), AnimationType::IDLE);
    ASSERT_EQ(ghostRunner.pdn->getLightManager()->getCurrentAnimation(), AnimationType::IDLE);
    ASSERT_EQ(spikeVector.pdn->getLightManager()->getCurrentAnimation(), AnimationType::IDLE);

    // Verify animation is running (color palette should be visible)
    ASSERT_TRUE(signalEcho.pdn->getLightManager()->isAnimating());
    ASSERT_TRUE(ghostRunner.pdn->getLightManager()->isAnimating());
    ASSERT_TRUE(spikeVector.pdn->getLightManager()->isAnimating());

    DeviceFactory::destroyDevice(spikeVector);
    DeviceFactory::destroyDevice(ghostRunner);
    DeviceFactory::destroyDevice(signalEcho);
}

#endif // NATIVE_BUILD
