// Native Driver Tests - native driver implementations

#pragma once

#include <gtest/gtest.h>
#include "device/drivers/native/native-display-driver.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-commands.hpp"
#include "apps/player-registration/player-registration-states.hpp"
#include "apps/player-registration/player-registration.hpp"

// ============================================
// NATIVE SERIAL DRIVER TEST SUITE
// ============================================

class NativeSerialDriverTestSuite : public testing::Test {
public:
    void SetUp() override {
        driver_ = new NativeSerialDriver("TestSerial");
    }

    void TearDown() override {
        delete driver_;
    }

    NativeSerialDriver* driver_;
};

void serialDriverOutputBufferMaxSize(NativeSerialDriverTestSuite* suite) {
    std::vector<uint8_t> longMsg(300, 'X');
    suite->driver_->writeBytes(longMsg.data(), longMsg.size());

    ASSERT_LE(suite->driver_->getOutputBufferSize(), NativeSerialDriver::MAX_OUTPUT_BUFFER_SIZE);
}

void serialDriverAvailableForWrite(NativeSerialDriverTestSuite* suite) {
    int initialCapacity = suite->driver_->availableForWrite();
    ASSERT_GT(initialCapacity, 0);

    uint8_t b = 'X';
    suite->driver_->writeBytes(&b, 1);

    int newCapacity = suite->driver_->availableForWrite();
    ASSERT_LT(newCapacity, initialCapacity);
}

void serialDriverClearOutput(NativeSerialDriverTestSuite* suite) {
    uint8_t msg[] = {'T', 'e', 's', 't'};
    suite->driver_->writeBytes(msg, sizeof(msg));
    ASSERT_GT(suite->driver_->getOutputBufferSize(), 0);

    suite->driver_->clearOutput();
    ASSERT_EQ(suite->driver_->getOutputBufferSize(), 0);
}

void serialDriverCallbackInvoked(NativeSerialDriverTestSuite* suite) {
    std::vector<uint8_t> received;
    suite->driver_->setBytesCallback([&](const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) received.push_back(data[i]);
    });

    uint8_t payload[] = {0xAA, 0x55, 0x42};
    suite->driver_->injectInputBytes(payload, sizeof(payload));
    suite->driver_->exec();

    ASSERT_EQ(received.size(), 3u);
    ASSERT_EQ(received[2], 0x42);
}

// ============================================
// NATIVE PEER COMMS DRIVER TEST SUITE
// ============================================

class NativePeerCommsDriverTestSuite : public testing::Test {
public:
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

void peerCommsTracksHistory(NativePeerCommsDriverTestSuite* suite) {
    uint8_t dstMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t data[] = {0x01, 0x02};
    
    suite->driver_->sendData(dstMac, PktType::kQuickdrawCommand, data, sizeof(data));
    
    const auto& history = suite->driver_->getPacketHistory();
    ASSERT_EQ(history.size(), 1);
    ASSERT_TRUE(history[0].isSent);
    ASSERT_EQ(history[0].packetType, PktType::kQuickdrawCommand);
}

void peerCommsStateString(NativePeerCommsDriverTestSuite* suite) {
    ASSERT_EQ(suite->driver_->getStateString(), "CONNECTED");

    suite->driver_->disconnect();
    ASSERT_EQ(suite->driver_->getStateString(), "DISCONNECTED");

    suite->driver_->connect();  // reconnect for teardown
}

void peerCommsMacString(NativePeerCommsDriverTestSuite* suite) {
    std::string macStr = suite->driver_->getMacString();

    ASSERT_EQ(macStr.length(), 17);  // 6 hex pairs + 5 colons
    ASSERT_EQ(macStr[2], ':');
    ASSERT_EQ(macStr[5], ':');
}

void peerCommsCannotSendWhenDisconnected(NativePeerCommsDriverTestSuite* suite) {
    suite->driver_->disconnect();

    uint8_t dstMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t data[] = {0x01};

    int result = suite->driver_->sendData(dstMac, PktType::kQuickdrawCommand, data, 1);
    ASSERT_EQ(result, -1);

    suite->driver_->connect();  // reconnect for teardown
}

void peerCommsHandlerRegistration(NativePeerCommsDriverTestSuite* suite) {
    bool handlerCalled = false;

    auto handler = [](const uint8_t* srcMac, const uint8_t* data, size_t length, void* ctx) {
        *static_cast<bool*>(ctx) = true;
    };

    suite->driver_->setPacketHandler(PktType::kQuickdrawCommand, handler, &handlerCalled);

    uint8_t srcMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t data[] = {0x01};
    suite->driver_->receivePacket(srcMac, PktType::kQuickdrawCommand, data, 1);
    suite->driver_->exec();  // exec drains the queue, invoking the handler

    ASSERT_TRUE(handlerCalled);

    handlerCalled = false;
    suite->driver_->clearPacketHandler(PktType::kQuickdrawCommand);
    suite->driver_->receivePacket(srcMac, PktType::kQuickdrawCommand, data, 1);
    suite->driver_->exec();  // cleared handler must not fire on drain
    ASSERT_FALSE(handlerCalled);
}

// ============================================
// NATIVE BUTTON DRIVER TEST SUITE
// ============================================

class NativeButtonDriverTestSuite : public testing::Test {
public:
    void SetUp() override {
        driver_ = new NativeButtonDriver("TestButton", 0);
    }
    
    void TearDown() override {
        delete driver_;
    }
    
    NativeButtonDriver* driver_;
};

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

void buttonDriverParameterizedCallback(NativeButtonDriverTestSuite* suite) {
    static int valueSet = 0;
    valueSet = 0;

    suite->driver_->setButtonPress([](void* ctx) {
        *static_cast<int*>(ctx) = 42;
    }, &valueSet, ButtonInteraction::PRESS);

    ASSERT_TRUE(suite->driver_->hasParameterizedCallback(ButtonInteraction::PRESS));

    suite->driver_->execCallback(ButtonInteraction::PRESS);
    ASSERT_EQ(valueSet, 42);
}

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
public:
    void SetUp() override {
        driver_ = new NativeLightStripDriver("TestLights");
    }
    
    void TearDown() override {
        delete driver_;
    }
    
    NativeLightStripDriver* driver_;
};

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

void lightStripBrightnessFloor(NativeLightStripDriverTestSuite* suite) {
    LEDState::SingleLEDState dimColor;
    dimColor.color = LEDColor(255, 0, 0);
    dimColor.brightness = 10;  // below the visibility floor
    
    suite->driver_->setLight(LightIdentifier::LEFT_LIGHTS, 0, dimColor);
    
    LEDState::SingleLEDState retrieved = suite->driver_->getLight(LightIdentifier::LEFT_LIGHTS, 0);
    
    ASSERT_GE(retrieved.brightness, NativeLightStripDriver::MIN_VISIBLE_BRIGHTNESS);
}

void lightStripGlobalBrightness(NativeLightStripDriverTestSuite* suite) {
    LEDState::SingleLEDState color;
    color.color = LEDColor(255, 255, 255);
    color.brightness = 255;
    
    suite->driver_->setLight(LightIdentifier::LEFT_LIGHTS, 0, color);
    suite->driver_->setLight(LightIdentifier::RIGHT_LIGHTS, 0, color);
    
    // setGlobalBrightness need not change stored per-light values; smoke test only
    suite->driver_->setGlobalBrightness(100);
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

// --- Text history, buffer, method chaining ---

void displayDriverDrawTextAddsToHistory(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("Hello");
    suite->driver_->drawText("World");

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 2u);
    ASSERT_EQ(history[0], "World");  // history is most-recent-first
    ASSERT_EQ(history[1], "Hello");
}

void displayDriverDrawTextPositionedAddsToHistory(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("Positioned", 10, 20);

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history[0], "Positioned");
}

void displayDriverDrawButtonAddsToHistory(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawButton("OK", 64, 32);

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history[0], "[OK]");
}

void displayDriverInvalidateScreenClearsBuffer(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("Fill");
    ASSERT_TRUE(suite->driver_->getPixel(0, 0));  // 'F' sets pixels at (0,0)

    suite->driver_->invalidateScreen();

    for (int y = 0; y < NativeDisplayDriver::HEIGHT; y += 16) {
        for (int x = 0; x < NativeDisplayDriver::WIDTH; x += 16) {
            ASSERT_FALSE(suite->driver_->getPixel(x, y));
        }
    }
}

void displayDriverTextHistoryMaxSize(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("One");
    suite->driver_->drawText("Two");
    suite->driver_->drawText("Three");
    suite->driver_->drawText("Four");
    suite->driver_->drawText("Five");

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 4u);  // capped at 4; "One" evicted
    ASSERT_EQ(history[0], "Five");
    ASSERT_EQ(history[3], "Two");
}

void displayDriverTextHistoryNoDuplicates(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->drawText("Same");
    suite->driver_->drawText("Same");
    suite->driver_->drawText("Same");

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
}

void displayDriverDrawTextRendersToBuffer(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();
    suite->driver_->drawText("A", 0, 0);

    // 'A' in 5x7 font: column 0 is 0x7E = 01111110, so bits 1-6 are on at x=0
    ASSERT_FALSE(suite->driver_->getPixel(0, 0));  // bit 0 off
    ASSERT_TRUE(suite->driver_->getPixel(0, 1));   // bit 1 on
    ASSERT_TRUE(suite->driver_->getPixel(0, 2));   // bit 2 on
}

void displayDriverMethodChainingWorks(NativeDisplayDriverTestSuite* suite) {
    // mirrors how game states chain display calls
    Display* result = suite->driver_->invalidateScreen()->drawText("Chain")->drawText("Test", 0, 10);

    ASSERT_EQ(result, static_cast<Display*>(suite->driver_));

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_GE(history.size(), 2u);
}

void displayDriverFontModeTracking(NativeDisplayDriverTestSuite* suite) {
    ASSERT_EQ(suite->driver_->getFontModeName(), "TEXT");

    suite->driver_->setGlyphMode(FontMode::NUMBER_GLYPH);
    ASSERT_EQ(suite->driver_->getFontModeName(), "NUMBER");

    suite->driver_->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    ASSERT_EQ(suite->driver_->getFontModeName(), "INV_SM");
}

// --- XBM decoding, mirror rendering ---

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

// drawImage(img) uses Image's own default position
void displayDriverImageOffset(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 64, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);

    ASSERT_FALSE(suite->driver_->getPixel(0, 0));
    ASSERT_TRUE(suite->driver_->getPixel(64, 0));
    ASSERT_TRUE(suite->driver_->getPixel(71, 0));
}

// -1,-1 are sentinels meaning "use Image's default position"
void displayDriverImageSentinelPosition(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 32, 10);  // default at (32, 10)

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img, -1, -1);

    ASSERT_FALSE(suite->driver_->getPixel(0, 0));
    ASSERT_TRUE(suite->driver_->getPixel(32, 10));
    ASSERT_TRUE(suite->driver_->getPixel(39, 10));
}

// explicit x,y overrides the Image's default position
void displayDriverImageExplicitPosition(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 64, 0);  // default at (64, 0)

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img, 10, 20);

    ASSERT_FALSE(suite->driver_->getPixel(64, 0));   // not at default
    ASSERT_TRUE(suite->driver_->getPixel(10, 20));   // at override
    ASSERT_TRUE(suite->driver_->getPixel(17, 20));
}

void displayDriverNullImageSafety(NativeDisplayDriverTestSuite* suite) {
    Image img(nullptr, 128, 64, 0, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);

    ASSERT_FALSE(suite->driver_->getPixel(0, 0));
}

// two overlaid images (the AwakenSequence pattern)
void displayDriverDoubleDrawImage(NativeDisplayDriverTestSuite* suite) {
    const unsigned char imgA[] = { 0xFF };
    Image imageA(imgA, 8, 1, 0, 0);

    const unsigned char imgB[] = { 0xFF };
    Image imageB(imgB, 8, 1, 64, 0);

    suite->driver_->invalidateScreen()->drawImage(imageA)->drawImage(imageB);

    ASSERT_TRUE(suite->driver_->getPixel(0, 0));    // imageA
    ASSERT_TRUE(suite->driver_->getPixel(7, 0));    // imageA
    ASSERT_FALSE(suite->driver_->getPixel(32, 0));  // gap between images
    ASSERT_TRUE(suite->driver_->getPixel(64, 0));   // imageB
    ASSERT_TRUE(suite->driver_->getPixel(71, 0));   // imageB
}

// text overlaid on an image (the Idle-state pattern)
void displayDriverDrawImageThenText(NativeDisplayDriverTestSuite* suite) {
    const unsigned char xbmData[] = { 0xFF };
    Image img(xbmData, 8, 1, 0, 0);

    suite->driver_->invalidateScreen();
    suite->driver_->drawImage(img);
    suite->driver_->drawText("Hi", 0, 10);

    ASSERT_TRUE(suite->driver_->getPixel(0, 0));   // image row still on
    ASSERT_TRUE(suite->driver_->getPixel(0, 11));  // 'H' pixel at bit 1

    const auto& history = suite->driver_->getTextHistory();
    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history[0], "Hi");
}

void displayDriverRenderToAsciiDimensions(NativeDisplayDriverTestSuite* suite) {
    auto lines = suite->driver_->renderToAscii(2, 2);

    // 64 height / (2*2) = 16 lines
    ASSERT_EQ(lines.size(), 16u);

    // blank screen: spaces (1 byte each), 128/2 = 64 chars per line
    for (const auto& line : lines) {
        ASSERT_EQ(line.size(), 64u);
    }
}

void displayDriverRenderToAsciiContent(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // upper half of the first char cell only -> upper half-block glyph
    unsigned char topOnly[1024];
    memset(topOnly, 0, sizeof(topOnly));
    topOnly[0] = 0x03;  // row 0, pixels 0,1 on
    Image img(topOnly, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToAscii(2, 2);

    std::string upperBlock = "\xe2\x96\x80";  // UTF-8 ▀
    ASSERT_GE(lines[0].size(), 3u);
    ASSERT_EQ(lines[0].substr(0, 3), upperBlock);
}

void displayDriverRenderToAsciiFullBlock(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // both halves of the first char cell -> full block
    unsigned char bothHalves[1024];
    memset(bothHalves, 0, sizeof(bothHalves));
    bothHalves[0] = 0x03;   // row 0, pixels 0,1 on
    bothHalves[32] = 0x03;  // row 2 (byte 2*16), pixels 0,1 on
    Image img(bothHalves, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToAscii(2, 2);

    std::string fullBlock = "\xe2\x96\x88";  // UTF-8 █
    ASSERT_GE(lines[0].size(), 3u);
    ASSERT_EQ(lines[0].substr(0, 3), fullBlock);
}

// --- Braille rendering tests ---

void displayDriverRenderToBrailleDimensions(NativeDisplayDriverTestSuite* suite) {
    auto lines = suite->driver_->renderToBraille();

    // 64 height / 4 = 16 lines
    ASSERT_EQ(lines.size(), 16u);

    // blank: U+2800 (3 bytes each), 128/2 = 64 chars = 192 bytes per line
    for (const auto& line : lines) {
        ASSERT_EQ(line.size(), 192u);
    }
}

void displayDriverRenderToBrailleBlank(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();
    auto lines = suite->driver_->renderToBraille();

    std::string emptyBraille = "\xe2\xa0\x80";  // U+2800 empty braille
    ASSERT_EQ(lines[0].substr(0, 3), emptyBraille);
    ASSERT_EQ(lines[15].substr(189, 3), emptyBraille);  // last char of last line
}

void displayDriverRenderToBrailleContent(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // pixel (0,0) maps to braille dot 1 (bit 0x01) -> U+2801
    const unsigned char singlePixel[1024] = {};
    unsigned char data[1024];
    memset(data, 0, sizeof(data));
    data[0] = 0x01;  // row 0, pixel 0 on
    Image img(data, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToBraille();

    std::string expected = "\xe2\xa0\x81";  // U+2801
    ASSERT_EQ(lines[0].substr(0, 3), expected);
}

void displayDriverRenderToBrailleFullBlock(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // all pixels set -> U+28FF (all 8 dots)
    unsigned char allOn[1024];
    memset(allOn, 0xFF, sizeof(allOn));
    Image img(allOn, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToBraille();

    std::string fullBraille = "\xe2\xa3\xbf";  // U+28FF
    ASSERT_EQ(lines[0].substr(0, 3), fullBraille);
    ASSERT_EQ(lines[8].substr(96, 3), fullBraille);  // mid-screen spot check
}

void displayDriverRenderToBrailleDotMapping(NativeDisplayDriverTestSuite* suite) {
    suite->driver_->invalidateScreen();

    // pixel (1,0) maps to braille dot 4 (bit 0x08) -> U+2808
    unsigned char data[1024];
    memset(data, 0, sizeof(data));
    data[0] = 0x02;  // row 0, pixel 1 on (LSB-first: bit1 = x=1)
    Image img(data, 128, 64, 0, 0);
    suite->driver_->drawImage(img);

    auto lines = suite->driver_->renderToBraille();

    std::string expected = "\xe2\xa0\x88";  // U+2808
    ASSERT_EQ(lines[0].substr(0, 3), expected);
}

// ============================================
// CLI DISPLAY COMMAND TEST SUITE
// ============================================

/* mirror/captions/display commands touch only the Renderer, so no devices needed. */
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

void cliCommandMirrorToggle(CliDisplayCommandTestSuite* suite) {
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());  // mirror defaults OFF

    suite->exec("mirror");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());

    suite->exec("mirror");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
}

void cliCommandMirrorOnOff(CliDisplayCommandTestSuite* suite) {
    suite->exec("mirror on");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());

    suite->exec("mirror off");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
}

void cliCommandCaptionsToggle(CliDisplayCommandTestSuite* suite) {
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());  // captions default ON

    suite->exec("captions");
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());

    suite->exec("captions");
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

void cliCommandCaptionsOnOff(CliDisplayCommandTestSuite* suite) {
    suite->exec("captions off");
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());

    suite->exec("captions on");
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// "cap" is an alias for captions
void cliCommandCaptionsAlias(CliDisplayCommandTestSuite* suite) {
    suite->exec("cap");
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());

    suite->exec("cap on");
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

void cliCommandDisplayOn(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(false);
    suite->renderer_.setCaptions(false);

    suite->exec("display on");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

void cliCommandDisplayOff(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(true);
    suite->renderer_.setCaptions(true);

    suite->exec("display off");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());
}

// when mirror and captions agree, toggle flips both: off -> on
void cliCommandDisplayToggleBothOff(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(false);
    suite->renderer_.setCaptions(false);

    suite->exec("display");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// agree case, other direction: on -> off
void cliCommandDisplayToggleBothOn(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(true);
    suite->renderer_.setCaptions(true);

    suite->exec("display");
    ASSERT_FALSE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_FALSE(suite->renderer_.isCaptionsEnabled());
}

// when mirror/captions disagree, toggle forces both on (mirror on, captions off)
void cliCommandDisplayToggleDisagree(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(true);
    suite->renderer_.setCaptions(false);

    suite->exec("display");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// disagree case, opposite start (mirror off, captions on) still forces both on
void cliCommandDisplayToggleDisagreeReverse(CliDisplayCommandTestSuite* suite) {
    suite->renderer_.setDisplayMirror(false);
    suite->renderer_.setCaptions(true);

    suite->exec("display");
    ASSERT_TRUE(suite->renderer_.isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_.isCaptionsEnabled());
}

// "d" is an alias for display
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

// Push a device back to the PR app's FetchUserData (game index 0, internal
// index 1). The CLI factory boots devices past registration into gameplay, so
// registration-flow tests (and cmdReboot mirrors) start here.
inline void skipToFetchUserData(cli::DeviceInstance& device) {
    device.game->skipToState(device.pdn, 0);
    State* state = device.game->getCurrentState();
    if (state && state->getStateId() == PLAYER_REGISTRATION_APP_ID) {
        static_cast<PlayerRegistrationApp*>(state)->skipToState(device.pdn, 1);
    }
}

class CliCommandTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("test_cmd_clock");
        globalLogger_ = new NativeLoggerDriver("test_cmd_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);
        device_ = cli::DeviceFactory::createDevice(0, true);

        skipToFetchUserData(device_);
    }

    void TearDown() override {
        cli::DeviceFactory::destroyDevice(device_);
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    cli::DeviceInstance device_;
    NativeClockDriver* globalClock_;
    NativeLoggerDriver* globalLogger_;
};

void cliDeviceMockHttpFetchTransitions(CliCommandTestSuite* suite) {
    State* state = suite->device_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);

    PlayerRegistrationApp* prApp = static_cast<PlayerRegistrationApp*>(state);
    State* prState = prApp->getCurrentState();
    ASSERT_NE(prState, nullptr);
    ASSERT_EQ(prState->getStateId(), FETCH_USER_DATA);

    // mock HTTP responds immediately, so a handful of loops completes the fetch
    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }

    state = suite->device_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);

    prApp = static_cast<PlayerRegistrationApp*>(state);
    prState = prApp->getCurrentState();
    ASSERT_NE(prState, nullptr);
    ASSERT_EQ(prState->getStateId(), WELCOME_MESSAGE);
}

void cliCommandRebootResetsState(CliCommandTestSuite* suite) {
    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }
    State* state = suite->device_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);

    PlayerRegistrationApp* prApp = static_cast<PlayerRegistrationApp*>(state);
    State* prState = prApp->getCurrentState();
    ASSERT_NE(prState, nullptr);
    ASSERT_NE(prState->getStateId(), FETCH_USER_DATA);

    // reboot, mirroring cmdReboot
    suite->device_.httpClientDriver->setMockServerEnabled(true);
    suite->device_.httpClientDriver->setConnected(true);
    suite->device_.stateHistory.clear();
    suite->device_.lastStateId = -1;

    skipToFetchUserData(suite->device_);

    state = suite->device_.game->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);

    prApp = static_cast<PlayerRegistrationApp*>(state);
    prState = prApp->getCurrentState();
    ASSERT_NE(prState, nullptr);
    ASSERT_EQ(prState->getStateId(), FETCH_USER_DATA);
}

// reboot from a later state must return to FetchUserData and re-transition cleanly
void cliCommandRebootFromLaterState(CliCommandTestSuite* suite) {
    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }

    State* state = suite->device_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);
    PlayerRegistrationApp* prApp = static_cast<PlayerRegistrationApp*>(state);
    ASSERT_EQ(prApp->getCurrentState()->getStateId(), WELCOME_MESSAGE);

    suite->device_.httpClientDriver->setMockServerEnabled(true);
    suite->device_.httpClientDriver->setConnected(true);
    suite->device_.stateHistory.clear();
    suite->device_.lastStateId = -1;

    skipToFetchUserData(suite->device_);

    state = suite->device_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);
    prApp = static_cast<PlayerRegistrationApp*>(state);
    ASSERT_EQ(prApp->getCurrentState()->getStateId(), FETCH_USER_DATA);

    for (int i = 0; i < 10; i++) {
        suite->device_.pdn->loop();
    }

    state = suite->device_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);
    prApp = static_cast<PlayerRegistrationApp*>(state);
    ASSERT_EQ(prApp->getCurrentState()->getStateId(), WELCOME_MESSAGE);
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

        SimpleTimer::setPlatformClock(nullptr);
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

void cliRoleCommandShowsSelectedDevice(CliRoleCommandTestSuite* suite) {
    suite->selectedDevice_ = 0;  // hunter
    auto result = suite->processor_->execute("role", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0010: Hunter");
}

void cliRoleCommandShowsSpecificDevice(CliRoleCommandTestSuite* suite) {
    suite->selectedDevice_ = 0;  // hunter selected, but query device 1 by index
    auto result = suite->processor_->execute("role 1", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0011: Bounty");

    // same device addressed by ID
    result = suite->processor_->execute("role 0011", suite->devices_,
                                        suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0011: Bounty");
}

void cliRoleCommandShowsAllDevices(CliRoleCommandTestSuite* suite) {
    auto result = suite->processor_->execute("role all", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0010 [0]: Hunter | Device 0011 [1]: Bounty");
}

// "roles" is an alias for role
void cliRoleCommandAliasWorks(CliRoleCommandTestSuite* suite) {
    suite->selectedDevice_ = 1;  // bounty
    auto result = suite->processor_->execute("roles", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Device 0011: Bounty");
}

void cliRoleCommandInvalidDevice(CliRoleCommandTestSuite* suite) {
    auto result = suite->processor_->execute("role 99", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "Invalid device");
}

void cliRoleCommandNoDevices(CliRoleCommandTestSuite* suite) {
    for (auto& dev : suite->devices_) {
        cli::DeviceFactory::destroyDevice(dev);
    }
    suite->devices_.clear();

    auto result = suite->processor_->execute("role all", suite->devices_,
                                             suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.shouldQuit);
    ASSERT_EQ(result.message, "No devices");
}

void cliCommandRebootClearsHistory(CliCommandTestSuite* suite) {
    suite->device_.stateHistory.push_back(1);
    suite->device_.stateHistory.push_back(5);
    suite->device_.stateHistory.push_back(7);
    suite->device_.lastStateId = 7;

    ASSERT_EQ(suite->device_.stateHistory.size(), 3u);
    ASSERT_EQ(suite->device_.lastStateId, 7);

    suite->device_.httpClientDriver->setMockServerEnabled(true);
    suite->device_.httpClientDriver->setConnected(true);
    suite->device_.stateHistory.clear();
    suite->device_.lastStateId = -1;

    skipToFetchUserData(suite->device_);

    ASSERT_TRUE(suite->device_.stateHistory.empty());
    ASSERT_EQ(suite->device_.lastStateId, -1);

    State* state = suite->device_.game->getCurrentState();
    ASSERT_EQ(state->getStateId(), PLAYER_REGISTRATION_APP_ID);
    PlayerRegistrationApp* prApp = static_cast<PlayerRegistrationApp*>(state);
    ASSERT_EQ(prApp->getCurrentState()->getStateId(), FETCH_USER_DATA);
}
