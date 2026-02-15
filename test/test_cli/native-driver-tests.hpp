//
// Native Driver Tests - Tests for native driver implementations
//

#pragma once

#include <gtest/gtest.h>
#include "device/drivers/native/native-display-driver.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-commands.hpp"
#include "game/quickdraw-states.hpp"

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

// ============================================
// NATIVE DISPLAY DRIVER TEST SUITE
// ============================================

class NativeDisplayDriverTestSuite : public testing::Test {
public:
    void SetUp() override {
        driver_ = new NativeDisplayDriver("TestDisplay");
    }

    void TearDown() override {
        delete driver_;
    }

    NativeDisplayDriver* driver_;
};

// --- Old behavior tests: text history, buffer, method chaining ---

// Test: drawText adds to text history
void displayDriverDrawTextAddsToHistory(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("Hello");
    suite->driver_->drawText("World");

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 2u);
    ASSERT_EQ(history[0], "World");  // Most recent first
    ASSERT_EQ(history[1], "Hello");
}

// Test: drawText with position also adds to text history
void displayDriverDrawTextPositionedAddsToHistory(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("Positioned", 10, 20);

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history[0], "Positioned");
}

// Test: drawButton adds bracketed text to history
void displayDriverDrawButtonAddsToHistory(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawButton("OK", 64, 32);

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history[0], "[OK]");
}

// Test: invalidateScreen clears the entire buffer
void displayDriverInvalidateScreenClearsBuffer(NativeDisplayDriverTestSuite* suite) {
    // Draw something first
    suite->driver_->drawText("Fill");
    ASSERT_TRUE(suite->driver_->getPixel(0, 0));  // 'F' sets pixels at (0,0)

    suite->driver_->invalidateScreen();

    // All pixels should be off
    for (int y = 0; y < NativeDisplayDriver::HEIGHT; y += 16) {
        for (int x = 0; x < NativeDisplayDriver::WIDTH; x += 16) {
            ASSERT_FALSE(suite->driver_->getPixel(x, y));
        }
    }
}

// Test: text history is capped at 4 entries
void displayDriverTextHistoryMaxSize(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("One");
    suite->driver_->drawText("Two");
    suite->driver_->drawText("Three");
    suite->driver_->drawText("Four");
    suite->driver_->drawText("Five");

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 4u);
    ASSERT_EQ(history[0], "Five");   // Most recent
    ASSERT_EQ(history[3], "Two");    // Oldest kept
}

// Test: duplicate consecutive text entries are not added
void displayDriverTextHistoryNoDuplicates(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("Same");
    suite->driver_->drawText("Same");
    suite->driver_->drawText("Same");

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
}

// Test: drawText renders pixels to the buffer
void displayDriverDrawTextRendersToBuffer(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();
    suite->driver_->drawText("A", 0, 0);

    // 'A' in 5x7 font: column 0 is 0x7E = 01111110
    // Bits 1-6 should be on at x=0
    ASSERT_FALSE(suite->driver_->getPixel(0, 0));  // bit 0 off
    ASSERT_TRUE(suite->driver_->getPixel(0, 1));   // bit 1 on
    ASSERT_TRUE(suite->driver_->getPixel(0, 2));   // bit 2 on
}

// Test: method chaining works (invalidateScreen->drawText->render)
void displayDriverMethodChainingWorks(NativeDisplayDriverTestSuite* suite) {
    // This mimics how game states use the display
    Display* result = suite->driver_->invalidateScreen()->drawText("Chain")->drawText("Test", 0, 10);

    // Should return this pointer for chaining
    ASSERT_EQ(result, static_cast<Display*>(suite->driver_));

    // Both texts should be in history
    const auto& history = suite->driver_->getTextHistory();
    ASSERT_GE(history.size(), 2u);
}

// Test: font mode tracking
void displayDriverFontModeTracking(NativeDisplayDriverTestSuite* suite) {
    ASSERT_EQ(suite->driver_->getFontModeName(), "TEXT");

    suite->driver_->setGlyphMode(FontMode::NUMBER_GLYPH);
    ASSERT_EQ(suite->driver_->getFontModeName(), "NUMBER");

    suite->driver_->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    ASSERT_EQ(suite->driver_->getFontModeName(), "INV_SM");
}

// --- New behavior tests: XBM decoding, mirror rendering ---

// Test: XBM byte decoding sets correct pixels (LSB-first)
void displayDriverXBMDecoding(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = {
        0xA5, // row 0: pixels 0,2,5,7 on (LSB-first: 10100101)
        0xFF, // row 1: all 8 pixels on
    };
    Image img(xbmData, 8, 2, 0, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);

    ASSERT_TRUE(suite->driver_->getPixel(0, 0));
    ASSERT_FALSE(suite->driver_->getPixel(1, 0));
    ASSERT_TRUE(suite->driver_->getPixel(2, 0));
    ASSERT_FALSE(suite->driver_->getPixel(3, 0));
    ASSERT_FALSE(suite->driver_->getPixel(4, 0));
    ASSERT_TRUE(suite->driver_->getPixel(5, 0));
    ASSERT_FALSE(suite->driver_->getPixel(6, 0));
    ASSERT_TRUE(suite->driver_->getPixel(7, 0));

    for (int x = 0; x < 8; x++) {
        ASSERT_TRUE(suite->driver_->getPixel(x, 1));
    }
}

// Test: Full 128x64 all-white image decodes correctly
void displayDriverFullWhiteImage(NativeDisplayDriverTestSuite* suite) {
    unsigned char whiteData[1024];
    memset(whiteData, 0xFF, sizeof(whiteData));
    Image img(whiteData, 128, 64, 0, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);

    ASSERT_TRUE(suite->driver_->getPixel(0, 0));
    ASSERT_TRUE(suite->driver_->getPixel(127, 0));
    ASSERT_TRUE(suite->driver_->getPixel(0, 63));
    ASSERT_TRUE(suite->driver_->getPixel(127, 63));
    ASSERT_TRUE(suite->driver_->getPixel(64, 32));
}

// Test: drawImage(img) uses Image's default position
void displayDriverImageOffset(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 64, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);

    ASSERT_FALSE(suite->driver_->getPixel(0, 0));
    ASSERT_TRUE(suite->driver_->getPixel(64, 0));
    ASSERT_TRUE(suite->driver_->getPixel(71, 0));
}

// Test: drawImage(img, -1, -1) uses Image's default position (sentinel values)
void displayDriverImageSentinelPosition(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 32, 10);  // default at (32, 10)

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img, -1, -1);

    ASSERT_FALSE(suite->driver_->getPixel(0, 0));
    ASSERT_TRUE(suite->driver_->getPixel(32, 10));
    ASSERT_TRUE(suite->driver_->getPixel(39, 10));
}

// Test: drawImage(img, x, y) overrides Image's default position
void displayDriverImageExplicitPosition(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 64, 0);  // default at (64, 0)

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img, 10, 20);  // override to (10, 20)

    ASSERT_FALSE(suite->driver_->getPixel(64, 0));   // Not at default
    ASSERT_TRUE(suite->driver_->getPixel(10, 20));    // At explicit position
    ASSERT_TRUE(suite->driver_->getPixel(17, 20));
}

// Test: Null image data doesn't crash
void displayDriverNullImageSafety(NativeDisplayDriverTestSuite* suite) {
    Image img(nullptr, 128, 64, 0, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);

    ASSERT_FALSE(suite->driver_->getPixel(0, 0));
}

// Test: Two drawImage calls overlay correctly (like AwakenSequence state)
void displayDriverDoubleDrawImage(NativeDisplayDriverTestSuite* suite) {
    // First image: 8px wide, sets pixels at (0,0)
    const unsigned char imgA[] = { 0xFF };
    Image imageA(imgA, 8, 1, 0, 0);

    // Second image: 8px wide, sets pixels at (64,0)
    const unsigned char imgB[] = { 0xFF };
    Image imageB(imgB, 8, 1, 64, 0);

    // Chain like game code: invalidateScreen()->drawImage(A)->drawImage(B)
    suite->driver_->invalidateScreen()->drawImage(imageA)->drawImage(imageB);

    // Both images should be present
    ASSERT_TRUE(suite->driver_->getPixel(0, 0));    // From imageA
    ASSERT_TRUE(suite->driver_->getPixel(7, 0));    // From imageA
    ASSERT_FALSE(suite->driver_->getPixel(32, 0));  // Gap between images
    ASSERT_TRUE(suite->driver_->getPixel(64, 0));   // From imageB
    ASSERT_TRUE(suite->driver_->getPixel(71, 0));   // From imageB
}

// Test: Text overlays on image correctly (like Idle state)
void displayDriverDrawImageThenText(NativeDisplayDriverTestSuite* suite) {
    // Draw a small image at origin
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 0, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);
    suite->driver_->drawText("Hi", 0, 10);

    // Image pixels at row 0 should still be on
    ASSERT_TRUE(suite->driver_->getPixel(0, 0));
    // Text pixels at row 10 should also be on
    ASSERT_TRUE(suite->driver_->getPixel(0, 11));  // 'H' has pixel at bit 1

    // Text should also be in history
    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history[0], "Hi");
}

// Test: renderToAscii output dimensions at 2x2 scale (blank screen)
void displayDriverRenderToAsciiDimensions(NativeDisplayDriverTestSuite* suite) {
    auto lines = suite->driver_->renderToAscii(2, 2);

    // 64 height / (2*2) = 16 lines
    ASSERT_EQ(lines.size(), 16u);

    // Blank screen: all spaces (1 byte each), 128/2 = 64 chars per line
    for (const auto& line : lines) {
        ASSERT_EQ(line.size(), 64u);
    }
}

// Test: renderToAscii produces correct half-block characters for known pixels
void displayDriverRenderToAsciiContent(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // At scale (2,2): character at column 0, line 0 covers pixels (0..1, 0..1) upper and (0..1, 2..3) lower
    // Set only upper-left pixel block: (0,0) and (1,0) on
    // This should produce upper half-block for the first character

    // Set pixels in upper half only (rows 0-1) of first char cell
    unsigned char topOnly[1024];
    memset(topOnly, 0, sizeof(topOnly));
    // Row 0: first two pixels on = 0x03 (bits 0,1)
    topOnly[0] = 0x03;
    Image img(topOnly, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToAscii(2, 2);

    // First character of first line should be upper half-block
    std::string upperBlock = "\xe2\x96\x80";  // UTF-8 for ▀
    ASSERT_GE(lines[0].size(), 3u);
    ASSERT_EQ(lines[0].substr(0, 3), upperBlock);
}

// Test: renderToAscii full block for pixels in both halves
void displayDriverRenderToAsciiFullBlock(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // Set pixels in both upper (rows 0-1) and lower (rows 2-3) of first char cell
    unsigned char bothHalves[1024];
    memset(bothHalves, 0, sizeof(bothHalves));
    bothHalves[0] = 0x03;  // Row 0: pixels 0,1 on
    // Row 2 byte index: 2 * 16 = 32, pixels 0,1 on
    bothHalves[32] = 0x03;
    Image img(bothHalves, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToAscii(2, 2);

    // First character should be full block
    std::string fullBlock = "\xe2\x96\x88";  // UTF-8 for █
    ASSERT_GE(lines[0].size(), 3u);
    ASSERT_EQ(lines[0].substr(0, 3), fullBlock);
}

// --- Braille rendering tests ---

// Test: renderToBraille output dimensions (blank screen)
void displayDriverRenderToBrailleDimensions(NativeDisplayDriverTestSuite* suite) {
    auto lines = suite->driver_->renderToBraille();

    // 64 height / 4 = 16 lines
    ASSERT_EQ(lines.size(), 16u);

    // Blank screen: all U+2800 (3 bytes each), 128/2 = 64 chars = 192 bytes per line
    for (const auto& line : lines) {
        ASSERT_EQ(line.size(), 192u);
    }
}

// Test: renderToBraille blank screen produces empty braille characters
void displayDriverRenderToBrailleBlank(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();
    auto lines = suite->driver_->renderToBraille();

    // Every character should be U+2800 (empty braille) = 0xE2 0xA0 0x80
    std::string emptyBraille = "\xe2\xa0\x80";
    ASSERT_EQ(lines[0].substr(0, 3), emptyBraille);
    ASSERT_EQ(lines[15].substr(189, 3), emptyBraille);  // Last char of last line
}

// Test: renderToBraille encodes individual pixel positions correctly
void displayDriverRenderToBrailleContent(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // Set pixel (0,0) — maps to braille dot 1 (bit 0x01) → U+2801
    const unsigned char singlePixel[1024] = {};
    unsigned char data[1024];
    memset(data, 0, sizeof(data));
    data[0] = 0x01;  // Row 0, pixel 0 on
    Image img(data, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToBraille();

    // First char should be U+2801 = 0xE2 0xA0 0x81
    std::string expected = "\xe2\xa0\x81";
    ASSERT_EQ(lines[0].substr(0, 3), expected);
}

// Test: renderToBraille full block when all 8 pixels in a 2x4 cell are set
void displayDriverRenderToBrailleFullBlock(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // Fill all pixels to produce U+28FF (all 8 dots)
    unsigned char allOn[1024];
    memset(allOn, 0xFF, sizeof(allOn));
    Image img(allOn, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToBraille();

    // Every character should be U+28FF = 0xE2 0xA3 0xBF
    std::string fullBraille = "\xe2\xa3\xbf";
    ASSERT_EQ(lines[0].substr(0, 3), fullBraille);
    ASSERT_EQ(lines[8].substr(96, 3), fullBraille);  // Mid-screen spot check
}

// Test: renderToBraille correctly maps right-column pixel to dot 4
void displayDriverRenderToBrailleDotMapping(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // Set pixel (1,0) — maps to braille dot 4 (bit 0x08) → U+2808
    unsigned char data[1024];
    memset(data, 0, sizeof(data));
    data[0] = 0x02;  // Row 0, pixel 1 on (LSB-first: bit1 = x=1)
    Image img(data, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToBraille();

    // First char should be U+2808 = 0xE2 0xA0 0x88
    std::string expected = "\xe2\xa0\x88";
    ASSERT_EQ(lines[0].substr(0, 3), expected);
}

// ============================================
// CLI DISPLAY COMMAND TEST SUITE
// ============================================

/*
 * Lightweight test suite for mirror/captions/display commands.
 * These commands only interact with the Renderer, so no devices needed.
 */
class CliDisplayCommandTestSuite : public testing::Test {
public:
    std::vector<cli::DeviceInstance> devices_;
    int selectedDevice_ = 0;
    cli::Renderer renderer_;
    cli::CommandProcessor processor_;

    cli::CommandResult exec(const std::string& cmd) {
        return processor_.execute(cmd, devices_, selectedDevice_, renderer_);
    }
};

// Test: mirror command toggles display mirror
void cliCommandMirrorToggle(CliDisplayCommandTestSuite* suite) {
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());  // Default OFF

    suite->exec("mirror");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());

    suite->exec("mirror");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
}

// Test: mirror on/off explicit arguments
void cliCommandMirrorOnOff(CliDisplayCommandTestSuite* suite) {
    suite->exec("mirror on");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());

    suite->exec("mirror off");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
}

// Test: captions command toggles captions
void cliCommandCaptionsToggle(CliDisplayCommandTestSuite* suite) {
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());  // Default ON

    suite->exec("captions");
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());

    suite->exec("captions");
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// Test: captions on/off explicit arguments
void cliCommandCaptionsOnOff(CliDisplayCommandTestSuite* suite) {
    suite->exec("captions off");
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());

    suite->exec("captions on");
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// Test: captions alias "cap" works
void cliCommandCaptionsAlias(CliDisplayCommandTestSuite* suite) {
    suite->exec("cap");
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());

    suite->exec("cap on");
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// Test: display on sets both mirror and captions on
void cliCommandDisplayOn(CliDisplayCommandTestSuite* suite) {
    // Start with both off
    suite->renderer_.setDisplayMirror(false);
    suite->renderer_.setCaptions(false);

    suite->exec("display on");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// Test: display off sets both mirror and captions off
void cliCommandDisplayOff(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(true);
    suite->renderer_.setCaptions(true);

    suite->exec("display off");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());
}

// Test: display toggle when both agree (both off -> both on)
void cliCommandDisplayToggleBothOff(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(false);
    suite->renderer_.setCaptions(false);

    suite->exec("display");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// Test: display toggle when both agree (both on -> both off)
void cliCommandDisplayToggleBothOn(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(true);
    suite->renderer_.setCaptions(true);

    suite->exec("display");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());
}

// Test: display toggle when mirror and captions disagree -> forces both on
void cliCommandDisplayToggleDisagree(CliDisplayCommandTestSuite* suite) {
    // Mirror on, captions off
    suite->renderer_.setDisplayMirror(true);
    suite->renderer_.setCaptions(false);

    suite->exec("display");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// Test: display toggle disagree (opposite direction)
void cliCommandDisplayToggleDisagreeReverse(CliDisplayCommandTestSuite* suite) {
    // Mirror off, captions on
    suite->renderer_.setDisplayMirror(false);
    suite->renderer_.setCaptions(true);

    suite->exec("display");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// Test: display alias "d" works
void cliCommandDisplayAlias(CliDisplayCommandTestSuite* suite) {
    suite->exec("d on");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());

    suite->exec("d off");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());
}

// ============================================
// CLI COMMAND / REBOOT TEST SUITE
// ============================================

class CliCommandTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("test_cmd_clock");
        globalLogger_ = new NativeLoggerDriver("test_cmd_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);
        device_ = cli::DeviceFactory::createDevice(0, true);
    }

    void TearDown() override {
        cli::DeviceFactory::destroyDevice(device_);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    cli::DeviceInstance device_;
    NativeClockDriver* globalClock_;
    NativeLoggerDriver* globalLogger_;
};

// Test: Mock HTTP fetch transitions device from FetchUserData to WelcomeMessage
void cliDeviceMockHttpFetchTransitions(CliCommandTestSuite* suite) {
    // Device starts at FetchUserData (state 1) after createDevice
    State* state = suite->device_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), FETCH_USER_DATA);

    // Run a few loop cycles — mock HTTP responds immediately
    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }

    // Should have transitioned to WelcomeMessage
    state = suite->device_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), WELCOME_MESSAGE);
}

// Test: Reboot resets device back to FetchUserData
void cliCommandRebootResetsState(CliCommandTestSuite* suite) {
    // Advance past FetchUserData
    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }
    State* state = suite->device_.game->getCurrentState();
    ASSERT_NE(state->getStateId(), FETCH_USER_DATA);

    // Reboot: same sequence as cmdReboot
    suite->device_.httpClientDriver->setMockServerEnabled(true);
    suite->device_.httpClientDriver->setConnected(true);
    suite->device_.stateHistory.clear();
    suite->device_.lastStateId = -1;
    suite->device_.game->skipToState(suite->device_.pdn, 1);

    // Should be back at FetchUserData
    state = suite->device_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), FETCH_USER_DATA);
}

// Test: Reboot from a later state returns to FetchUserData and can re-transition
void cliCommandRebootFromLaterState(CliCommandTestSuite* suite) {
    // Advance device further (past FetchUserData into WelcomeMessage or beyond)
    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }
    ASSERT_EQ(suite->device_.game->getCurrentState()->getStateId(), WELCOME_MESSAGE);

    // Reboot
    suite->device_.httpClientDriver->setMockServerEnabled(true);
    suite->device_.httpClientDriver->setConnected(true);
    suite->device_.stateHistory.clear();
    suite->device_.lastStateId = -1;
    suite->device_.game->skipToState(suite->device_.pdn, 1);

    ASSERT_EQ(suite->device_.game->getCurrentState()->getStateId(), FETCH_USER_DATA);

    // Run loops again — should transition to WelcomeMessage again
    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }
    ASSERT_EQ(suite->device_.game->getCurrentState()->getStateId(), WELCOME_MESSAGE);
}

// ============================================
// CLI ROLE COMMAND TEST SUITE
// ============================================

class CliRoleCommandTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("test_role_clock");
        globalLogger_ = new NativeLoggerDriver("test_role_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);

        // Create two devices - hunter and bounty
        devices_.push_back(cli::DeviceFactory::createDevice(0, true));   // Hunter
        devices_.push_back(cli::DeviceFactory::createDevice(1, false));  // Bounty

        selectedDevice_ = 0;
        processor_ = new cli::CommandProcessor();
        renderer_ = new cli::Renderer();
    }

    void TearDown() override {
        delete renderer_;
        delete processor_;

        for (auto& dev : devices_) {
            cli::DeviceFactory::destroyDevice(dev);
        }
        devices_.clear();

        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    std::vector<cli::DeviceInstance> devices_;
    int selectedDevice_;
    cli::CommandProcessor* processor_;
    cli::Renderer* renderer_;
    NativeClockDriver* globalClock_;
    NativeLoggerDriver* globalLogger_;
};

// Test: role command shows selected device's role
void cliRoleCommandShowsSelectedDevice(CliRoleCommandTestSuite* suite) {
    // Selected device is 0 (hunter)
    suite->selectedDevice_ = 0;
    auto result = suite->processor_->execute("role", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0010: Hunter");
}

// Test: role <device> shows specific device's role
void cliRoleCommandShowsSpecificDevice(CliRoleCommandTestSuite* suite) {
    // Request bounty device by index
    suite->selectedDevice_ = 0;  // Hunter selected, but we ask for device 1
    auto result = suite->processor_->execute("role 1", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0011: Bounty");

    // Request bounty device by ID
    result = suite->processor_->execute("role 0011", suite->devices_,
                                        suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0011: Bounty");
}

// Test: role all shows all devices
void cliRoleCommandShowsAllDevices(CliRoleCommandTestSuite* suite) {
    auto result = suite->processor_->execute("role all", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0010 [0]: Hunter | Device 0011 [1]: Bounty");
}

// Test: roles alias works
void cliRoleCommandAliasWorks(CliRoleCommandTestSuite* suite) {
    suite->selectedDevice_ = 1;  // Bounty selected
    auto result = suite->processor_->execute("roles", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0011: Bounty");
}

// Test: role with invalid device shows error
void cliRoleCommandInvalidDevice(CliRoleCommandTestSuite* suite) {
    auto result = suite->processor_->execute("role 99", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Invalid device");
}

// Test: role all with no devices shows "No devices"
void cliRoleCommandNoDevices(CliRoleCommandTestSuite* suite) {
    // Clear devices
    for (auto& dev : suite->devices_) {
        cli::DeviceFactory::destroyDevice(dev);
    }
    suite->devices_.clear();

    auto result = suite->processor_->execute("role all", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "No devices");
}

// Test: Reboot clears state history and resets lastStateId
void cliCommandRebootClearsHistory(CliCommandTestSuite* suite) {
    // Add some fake state history
    suite->device_.stateHistory.push_back(1);
    suite->device_.stateHistory.push_back(5);
    suite->device_.stateHistory.push_back(7);
    suite->device_.lastStateId = 7;

    ASSERT_EQ(suite->device_.stateHistory.size(), 3u);
    ASSERT_EQ(suite->device_.lastStateId, 7);

    // Reboot
    suite->device_.httpClientDriver->setMockServerEnabled(true);
    suite->device_.httpClientDriver->setConnected(true);
    suite->device_.stateHistory.clear();
    suite->device_.lastStateId = -1;
    suite->device_.game->skipToState(suite->device_.pdn, 1);

    // History should be cleared
    ASSERT_TRUE(suite->device_.stateHistory.empty());
    ASSERT_EQ(suite->device_.lastStateId, -1);

    // Device should be at FetchUserData
    ASSERT_EQ(suite->device_.game->getCurrentState()->getStateId(), FETCH_USER_DATA);
}
