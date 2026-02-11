#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"

using namespace cli;

class CliFdnTestSuite : public testing::Test {
protected:
    void SetUp() override {
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
    ASSERT_EQ(fdn0.deviceId, "7010");
    ASSERT_EQ(fdn1.deviceId, "7011");
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

#endif // NATIVE_BUILD
