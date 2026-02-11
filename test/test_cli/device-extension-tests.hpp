#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"

using namespace cli;

class DeviceExtensionTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Fresh test setup
        device_ = DeviceFactory::createDevice(0, true);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
    }

    DeviceInstance device_;
};

// Test: getApp returns registered app
void deviceExtensionGetAppReturnsApp(DeviceExtensionTestSuite* suite) {
    StateMachine* app = suite->device_.pdn->getApp(StateId(QUICKDRAW_APP_ID));
    ASSERT_NE(app, nullptr);
    ASSERT_EQ(app, suite->device_.game);
}

// Test: getApp returns nullptr for unknown app
void deviceExtensionGetAppReturnsNull(DeviceExtensionTestSuite* suite) {
    StateMachine* app = suite->device_.pdn->getApp(StateId(999));
    ASSERT_EQ(app, nullptr);
}

class DummyState : public State {
public:
    DummyState() : State(50) {}
    void onStateMounted(Device*) override { mounted = true; }
    bool mounted = false;
};

class DummySM : public StateMachine {
public:
    DummySM() : StateMachine(50) {}
    void populateStateMap() override {
        stateMap.push_back(new DummyState());
    }
};

// Test: returnToPreviousApp works after setActiveApp
void deviceExtensionReturnToPrevious(DeviceExtensionTestSuite* suite) {
    auto* dummy = new DummySM();
    AppConfig apps = {
        {StateId(QUICKDRAW_APP_ID), suite->device_.game},
        {StateId(50), dummy}
    };
    suite->device_.pdn->loadAppConfig(apps, StateId(QUICKDRAW_APP_ID));

    // Switch to dummy app
    suite->device_.pdn->setActiveApp(StateId(50));
    ASSERT_TRUE(suite->device_.game->isPaused());

    // Return to previous
    suite->device_.pdn->returnToPreviousApp();
    ASSERT_FALSE(suite->device_.game->isPaused());

    delete dummy;
}

#endif // NATIVE_BUILD
