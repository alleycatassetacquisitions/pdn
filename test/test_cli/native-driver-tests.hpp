//
// Native Driver Tests - Tests for native driver implementations
//

#pragma once

#include <gtest/gtest.h>
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"

// ============================================
// NATIVE SERIAL DRIVER TEST SUITE
// ============================================

class NativeSerialDriverTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        driver_ = new NativeSerialDriver("TestSerial");
    }
    
    void TearDown() override {
        delete driver_;
    }
    
    NativeSerialDriver* driver_;
};

// Test: Output buffer has max size limit
void serialDriverOutputBufferMaxSize(NativeSerialDriverTestSuite* suite) {
    // Write a very long message
    std::string longMsg(300, 'X');
    suite->driver_->println(longMsg);
    
    // Should be capped at max size
    ASSERT_LE(suite->driver_->getOutputBufferSize(), NativeSerialDriver::MAX_OUTPUT_BUFFER_SIZE);
}

// Test: Input queue enforces FIFO limits
void serialDriverInputQueueFIFO(NativeSerialDriverTestSuite* suite) {
    // Inject more than max queue size
    for (int i = 0; i < 50; i++) {
        suite->driver_->injectInput("MSG" + std::to_string(i));
    }
    
    // Queue should be capped
    ASSERT_LE(suite->driver_->getInputQueueSize(), NativeSerialDriver::MAX_INPUT_QUEUE_SIZE);
}

// Test: Available for write reflects buffer state
void serialDriverAvailableForWrite(NativeSerialDriverTestSuite* suite) {
    int initialCapacity = suite->driver_->availableForWrite();
    ASSERT_GT(initialCapacity, 0);
    
    // Write something
    suite->driver_->print('X');
    
    int newCapacity = suite->driver_->availableForWrite();
    ASSERT_LT(newCapacity, initialCapacity);
}

// Test: Message history is tracked
void serialDriverTracksHistory(NativeSerialDriverTestSuite* suite) {
    const char* msg = "TestMessage";
    suite->driver_->println(const_cast<char*>(msg));
    
    const auto& sentHistory = suite->driver_->getSentHistory();
    ASSERT_GE(sentHistory.size(), 1);
    ASSERT_EQ(sentHistory.back(), "TestMessage");
    
    suite->driver_->injectInput("ReceivedMsg");
    
    const auto& receivedHistory = suite->driver_->getReceivedHistory();
    ASSERT_GE(receivedHistory.size(), 1);
}

// Test: Clear output clears buffer
void serialDriverClearOutput(NativeSerialDriverTestSuite* suite) {
    const char* msg = "TestMessage";
    suite->driver_->println(const_cast<char*>(msg));
    ASSERT_GT(suite->driver_->getOutputBufferSize(), 0);
    
    suite->driver_->clearOutput();
    ASSERT_EQ(suite->driver_->getOutputBufferSize(), 0);
}

// Test: Callback is invoked on input
void serialDriverCallbackInvoked(NativeSerialDriverTestSuite* suite) {
    bool callbackCalled = false;
    std::string receivedMsg;
    
    suite->driver_->setStringCallback([&](const std::string& msg) {
        callbackCalled = true;
        receivedMsg = msg;
    });
    
    suite->driver_->injectInput("CallbackTest");
    
    ASSERT_TRUE(callbackCalled);
    ASSERT_EQ(receivedMsg, "CallbackTest");
}

// Test: Framing is stripped on injection
void serialDriverStripsFraming(NativeSerialDriverTestSuite* suite) {
    std::string receivedMsg;
    
    suite->driver_->setStringCallback([&](const std::string& msg) {
        receivedMsg = msg;
    });
    
    // Inject framed message (* at start, \r at end)
    suite->driver_->injectInput("*FRAMED_MSG\r");
    
    // Should receive without framing
    ASSERT_EQ(receivedMsg, "FRAMED_MSG");
}

// ============================================
// NATIVE PEER COMMS DRIVER TEST SUITE
// ============================================

class NativePeerCommsDriverTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        driver_ = new NativePeerCommsDriver("TestPeerComms");
        driver_->initialize();
        driver_->connect();
    }
    
    void TearDown() override {
        driver_->disconnect();
        delete driver_;
    }
    
    NativePeerCommsDriver* driver_;
};

// Test: Packet history is tracked
void peerCommsTracksHistory(NativePeerCommsDriverTestSuite* suite) {
    uint8_t dstMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t data[] = {0x01, 0x02};
    
    suite->driver_->sendData(dstMac, PktType::kQuickdrawCommand, data, sizeof(data));
    
    const auto& history = suite->driver_->getPacketHistory();
    ASSERT_EQ(history.size(), 1);
    ASSERT_TRUE(history[0].isSent);
    ASSERT_EQ(history[0].packetType, PktType::kQuickdrawCommand);
}

// Test: State string returns correct values
void peerCommsStateString(NativePeerCommsDriverTestSuite* suite) {
    ASSERT_EQ(suite->driver_->getStateString(), "CONNECTED");
    
    suite->driver_->disconnect();
    ASSERT_EQ(suite->driver_->getStateString(), "DISCONNECTED");
    
    suite->driver_->connect();  // Reconnect for teardown
}

// Test: MAC string format
void peerCommsMacString(NativePeerCommsDriverTestSuite* suite) {
    std::string macStr = suite->driver_->getMacString();
    
    // Should be in XX:XX:XX:XX:XX:XX format
    ASSERT_EQ(macStr.length(), 17);  // 6 hex pairs + 5 colons
    ASSERT_EQ(macStr[2], ':');
    ASSERT_EQ(macStr[5], ':');
}

// Test: Cannot send when disconnected
void peerCommsCannotSendWhenDisconnected(NativePeerCommsDriverTestSuite* suite) {
    suite->driver_->disconnect();
    
    uint8_t dstMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t data[] = {0x01};
    
    int result = suite->driver_->sendData(dstMac, PktType::kQuickdrawCommand, data, 1);
    ASSERT_EQ(result, -1);  // Should fail
    
    suite->driver_->connect();  // Reconnect for teardown
}

// Test: Handler registration and clearance
void peerCommsHandlerRegistration(NativePeerCommsDriverTestSuite* suite) {
    bool handlerCalled = false;
    
    auto handler = [](const uint8_t* srcMac, const uint8_t* data, size_t length, void* ctx) {
        *static_cast<bool*>(ctx) = true;
    };
    
    suite->driver_->setPacketHandler(PktType::kQuickdrawCommand, handler, &handlerCalled);
    
    // Simulate receiving a packet
    uint8_t srcMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t data[] = {0x01};
    suite->driver_->receivePacket(srcMac, PktType::kQuickdrawCommand, data, 1);
    
    ASSERT_TRUE(handlerCalled);
    
    // Clear handler
    handlerCalled = false;
    suite->driver_->clearPacketHandler(PktType::kQuickdrawCommand);
    suite->driver_->receivePacket(srcMac, PktType::kQuickdrawCommand, data, 1);
    ASSERT_FALSE(handlerCalled);  // Should not be called after clear
}

// ============================================
// NATIVE BUTTON DRIVER TEST SUITE
// ============================================

class NativeButtonDriverTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        driver_ = new NativeButtonDriver("TestButton", 0);  // pin 0
    }
    
    void TearDown() override {
        delete driver_;
    }
    
    NativeButtonDriver* driver_;
};

// Test: Callback registration and execution
void buttonDriverCallbackExecution(NativeButtonDriverTestSuite* suite) {
    static bool callbackCalled = false;
    callbackCalled = false;
    
    suite->driver_->setButtonPress([]() {
        callbackCalled = true;
    }, ButtonInteraction::PRESS);
    
    ASSERT_TRUE(suite->driver_->hasCallback(ButtonInteraction::PRESS));
    
    suite->driver_->execCallback(ButtonInteraction::PRESS);
    ASSERT_TRUE(callbackCalled);
}

// Test: Parameterized callback
void buttonDriverParameterizedCallback(NativeButtonDriverTestSuite* suite) {
    static int valueSet = 0;
    valueSet = 0;
    
    suite->driver_->setButtonPress([](void* ctx) {
        *static_cast<int*>(ctx) = 42;
    }, &valueSet, ButtonInteraction::PRESS);
    
    ASSERT_TRUE(suite->driver_->hasParameterizedCallback(ButtonInteraction::PRESS));
    
    // Execute with stored context
    suite->driver_->execCallback(ButtonInteraction::PRESS);
    ASSERT_EQ(valueSet, 42);
}

// Test: Remove callbacks
void buttonDriverRemoveCallbacks(NativeButtonDriverTestSuite* suite) {
    suite->driver_->setButtonPress([]() {}, ButtonInteraction::PRESS);
    suite->driver_->setButtonPress([]() {}, ButtonInteraction::LONG_PRESS);
    
    ASSERT_TRUE(suite->driver_->hasCallback(ButtonInteraction::PRESS));
    ASSERT_TRUE(suite->driver_->hasCallback(ButtonInteraction::LONG_PRESS));
    
    suite->driver_->removeButtonCallbacks();
    
    ASSERT_FALSE(suite->driver_->hasCallback(ButtonInteraction::PRESS));
    ASSERT_FALSE(suite->driver_->hasCallback(ButtonInteraction::LONG_PRESS));
}

// ============================================
// NATIVE LIGHT STRIP DRIVER TEST SUITE
// ============================================

class NativeLightStripDriverTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        driver_ = new NativeLightStripDriver("TestLights");
    }
    
    void TearDown() override {
        delete driver_;
    }
    
    NativeLightStripDriver* driver_;
};

// Test: Set and get light state
void lightStripSetAndGet(NativeLightStripDriverTestSuite* suite) {
    LEDState::SingleLEDState color;
    color.color = LEDColor(255, 128, 64);
    color.brightness = 200;
    
    suite->driver_->setLight(LightIdentifier::LEFT_LIGHTS, 0, color);
    
    LEDState::SingleLEDState retrieved = suite->driver_->getLight(LightIdentifier::LEFT_LIGHTS, 0);
    
    ASSERT_EQ(retrieved.color.red, 255);
    ASSERT_EQ(retrieved.color.green, 128);
    ASSERT_EQ(retrieved.color.blue, 64);
}

// Test: Clear resets all lights
void lightStripClear(NativeLightStripDriverTestSuite* suite) {
    LEDState::SingleLEDState color;
    color.color = LEDColor(255, 255, 255);
    color.brightness = 255;
    
    suite->driver_->setLight(LightIdentifier::LEFT_LIGHTS, 0, color);
    suite->driver_->setLight(LightIdentifier::RIGHT_LIGHTS, 0, color);
    
    suite->driver_->clear();
    
    LEDState::SingleLEDState left = suite->driver_->getLight(LightIdentifier::LEFT_LIGHTS, 0);
    LEDState::SingleLEDState right = suite->driver_->getLight(LightIdentifier::RIGHT_LIGHTS, 0);
    
    ASSERT_EQ(left.color.red, 0);
    ASSERT_EQ(left.brightness, 0);
    ASSERT_EQ(right.color.red, 0);
}

// Test: Brightness floor for visibility
void lightStripBrightnessFloor(NativeLightStripDriverTestSuite* suite) {
    LEDState::SingleLEDState dimColor;
    dimColor.color = LEDColor(255, 0, 0);
    dimColor.brightness = 10;  // Very dim
    
    suite->driver_->setLight(LightIdentifier::LEFT_LIGHTS, 0, dimColor);
    
    LEDState::SingleLEDState retrieved = suite->driver_->getLight(LightIdentifier::LEFT_LIGHTS, 0);
    
    // Should have brightness floor applied
    ASSERT_GE(retrieved.brightness, NativeLightStripDriver::MIN_VISIBLE_BRIGHTNESS);
}

// Test: Global brightness
void lightStripGlobalBrightness(NativeLightStripDriverTestSuite* suite) {
    LEDState::SingleLEDState color;
    color.color = LEDColor(255, 255, 255);
    color.brightness = 255;
    
    suite->driver_->setLight(LightIdentifier::LEFT_LIGHTS, 0, color);
    suite->driver_->setLight(LightIdentifier::RIGHT_LIGHTS, 0, color);
    
    suite->driver_->setGlobalBrightness(100);
    
    // Note: Global brightness affects the lights but may not change stored values
    // This is just a smoke test that the method exists and doesn't crash
}
