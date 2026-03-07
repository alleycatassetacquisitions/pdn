#pragma once
#include <gtest/gtest.h>
#include "device-mock.hpp"
#include "device/remote-device-coordinator.hpp"

class SerialRouterTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(testing::Return(1));

        device.rdcOverride = &rdc;
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
};

inline void rdcRoutesChainPrefixToOutputJackHandler(SerialRouterTests* t) {
    std::string received;
    t->rdc.registerSerialHandler("chain:", SerialIdentifier::OUTPUT_JACK,
        [&](const std::string& msg) { received = msg; });
    t->device.outputJackSerial.stringCallback("chain:0");
    EXPECT_EQ(received, "chain:0");
}

inline void rdcRoutesMessagesOnInputJackIndependently(SerialRouterTests* t) {
    std::string received;
    t->rdc.registerSerialHandler("chain:", SerialIdentifier::INPUT_JACK,
        [&](const std::string& msg) { received = msg; });
    t->device.inputJackSerial.stringCallback("chain:1");
    EXPECT_EQ(received, "chain:1");
}

inline void rdcIgnoresUnmatchedMessages(SerialRouterTests* t) {
    bool called = false;
    t->rdc.registerSerialHandler("chain:", SerialIdentifier::OUTPUT_JACK,
        [&](const std::string& msg) { called = true; });
    t->device.outputJackSerial.stringCallback("event:draw");
    EXPECT_FALSE(called);
}

inline void rdcRoutesMultiplePrefixesOnSameJack(SerialRouterTests* t) {
    std::string chainMsg, eventMsg;
    t->rdc.registerSerialHandler("chain:", SerialIdentifier::OUTPUT_JACK,
        [&](const std::string& msg) { chainMsg = msg; });
    t->rdc.registerSerialHandler("event:", SerialIdentifier::OUTPUT_JACK,
        [&](const std::string& msg) { eventMsg = msg; });
    t->device.outputJackSerial.stringCallback("chain:0");
    t->device.outputJackSerial.stringCallback("event:draw");
    EXPECT_EQ(chainMsg, "chain:0");
    EXPECT_EQ(eventMsg, "event:draw");
}

inline void rdcUnregisterRemovesHandler(SerialRouterTests* t) {
    bool called = false;
    t->rdc.registerSerialHandler("chain:", SerialIdentifier::OUTPUT_JACK,
        [&](const std::string& msg) { called = true; });
    t->rdc.unregisterSerialHandler("chain:", SerialIdentifier::OUTPUT_JACK);
    t->device.outputJackSerial.stringCallback("chain:0");
    EXPECT_FALSE(called);
}
