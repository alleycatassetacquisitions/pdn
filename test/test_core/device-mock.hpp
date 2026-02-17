//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include <gmock/gmock.h>
#include "device/device.hpp"
#include "device/drivers/display.hpp"
#include "device/drivers/button.hpp"
#include "device/drivers/haptics.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/light-manager.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include <queue>
#include <vector>

using namespace std;

class FakeHWSerialWrapper : public HWSerialWrapper {
    public:
    FakeHWSerialWrapper() : HWSerialWrapper() {}

    int availableForWrite() override {
        return 1024 - msgQueue.size();
    }

    int available() override {
        return msgQueue.size() > 0;
    }

    int peek() override {
        return msgQueue.front();
    }

    int read() override {
        char val = msgQueue.front();
        msgQueue.pop_front();
        return val;
    }

    std::string readStringUntil(char terminator) override {
        vector<char> buffer;
        while (msgQueue.front() != terminator) {
            buffer.push_back(msgQueue.front());
            msgQueue.pop_front();
        }
        msgQueue.pop_front();
        return std::string(&buffer.front(), buffer.size());
    }

    void print(char msg) override {
        msgQueue.emplace_back(msg);
    }

    void println(char* msg) override {
        while(msg[0] != '\0') {
            print(*msg);
        }
        print(STRING_TERM);
    }

    void println(const std::string& msg) override {
        const char* str = msg.c_str();
        for(size_t i = 0; i < msg.length(); i++) {
            print(str[i]);
        }
        print(STRING_TERM);
    }

    void flush() override {
        msgQueue.clear();
    }

    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback = callback;
    }

    deque<char> msgQueue;
    SerialStringCallback stringCallback;
};

class FakeDevice : public Device {
protected:
    HWSerialWrapper* outputJackSerial;
    HWSerialWrapper* inputJackSerial;

    HWSerialWrapper* outputJack() override {
        return outputJackSerial;
    }

    HWSerialWrapper* inputJack() override {
        return inputJackSerial;
    }
};

// Mock classes for each interface
class MockDisplay : public Display {
public:
    MOCK_METHOD(Display*, invalidateScreen, (), (override));
    MOCK_METHOD(void, render, (), (override));
    MOCK_METHOD(Display*, drawText, (const char*), (override));
    MOCK_METHOD(Display*, setGlyphMode, (FontMode), (override));
    MOCK_METHOD(Display*, renderGlyph, (const char*, int, int), (override));
    MOCK_METHOD(Display*, drawButton, (const char*, int, int), (override));
    MOCK_METHOD(Display*, drawText, (const char*, int, int), (override));
    MOCK_METHOD(Display*, drawImage, (Image), (override));
    MOCK_METHOD(Display*, drawImage, (Image, int, int), (override));
    MOCK_METHOD(Display*, drawBox, (int, int, int, int), (override));
    MOCK_METHOD(Display*, drawFrame, (int, int, int, int), (override));
    MOCK_METHOD(Display*, drawGlyph, (int, int, uint16_t), (override));
    MOCK_METHOD(Display*, setDrawColor, (int), (override));
    MOCK_METHOD(Display*, setFont, (const uint8_t*), (override));
};

class MockButton : public Button {
public:
    MOCK_METHOD(void, setButtonPress, (callbackFunction, ButtonInteraction), (override));
    MOCK_METHOD(void, setButtonPress, (parameterizedCallbackFunction, void*, ButtonInteraction), (override));
    MOCK_METHOD(void, removeButtonCallbacks, (), (override));
    MOCK_METHOD(bool, isLongPressed, (), (override));
    MOCK_METHOD(unsigned long, longPressedMillis, (), (override));
};

class MockHaptics : public Haptics {
public:
    MOCK_METHOD(bool, isOn, (), (override));
    MOCK_METHOD(void, max, (), (override));
    MOCK_METHOD(void, setIntensity, (int), (override));
    MOCK_METHOD(int, getIntensity, (), (override));
    MOCK_METHOD(void, off, (), (override));
};

class MockHttpClient : public HttpClientInterface {
public:
    MOCK_METHOD(void, setWifiConfig, (WifiConfig*), (override));
    MOCK_METHOD(bool, isConnected, (), (override));
    MOCK_METHOD(bool, queueRequest, (HttpRequest&), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(void, updateConfig, (WifiConfig*), (override));
    MOCK_METHOD(void, retryConnection, (), (override));
    MOCK_METHOD(uint8_t*, getMacAddress, (), (override));
    MOCK_METHOD(void, setHttpClientState, (HttpClientState), (override));
    MOCK_METHOD(HttpClientState, getHttpClientState, (), (override));
};

class MockPeerComms : public PeerCommsInterface {
public:
    MOCK_METHOD(int, sendData, (const uint8_t*, PktType, const uint8_t*, const size_t), (override));
    MOCK_METHOD(void, setPacketHandler, (PktType, PacketCallback, void*), (override));
    MOCK_METHOD(void, clearPacketHandler, (PktType), (override));
    MOCK_METHOD(const uint8_t*, getGlobalBroadcastAddress, (), (override));
    MOCK_METHOD(uint8_t*, getMacAddress, (), (override));
    MOCK_METHOD(void, removePeer, (uint8_t*), (override));
    MOCK_METHOD(void, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(void, setPeerCommsState, (PeerCommsState), (override));
    MOCK_METHOD(PeerCommsState, getPeerCommsState, (), (override));
};

class MockStorage : public StorageInterface {
public:
    MOCK_METHOD(size_t, write, (const std::string&, const std::string&), (override));
    MOCK_METHOD(std::string, read, (const std::string&, const std::string&), (override));
    MOCK_METHOD(bool, remove, (const std::string&), (override));
    MOCK_METHOD(bool, clear, (), (override));
    MOCK_METHOD(void, end, (), (override));
    MOCK_METHOD(uint8_t, readUChar, (const std::string&, uint8_t), (override));
    MOCK_METHOD(size_t, writeUChar, (const std::string&, uint8_t), (override));
};

// Mock QuickdrawWirelessManager for MatchManager tests
class MockQuickdrawWirelessManager : public QuickdrawWirelessManager {
public:
    MockQuickdrawWirelessManager() : QuickdrawWirelessManager() {}
};

// Fake light strip for LightManager
class FakeLightStrip : public LightStrip {
public:
    void setLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {}
    void setLightBrightness(LightIdentifier lightSet, uint8_t index, uint8_t brightness) override {}
    void setGlobalBrightness(uint8_t brightness) override {}
    LEDState::SingleLEDState getLight(LightIdentifier lightSet, uint8_t index) override { 
        return LEDState::SingleLEDState(); 
    }
    void fade(LightIdentifier lightSet, uint8_t fadeAmount) override {}
    void addToLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {}
    void setFPS(uint8_t fps) override {}
    uint8_t getFPS() const override { return 0; }
};

class MockDevice : public Device {
public:
    MockDevice() : Device(DriverConfig{}) {
        // Initialize mock pointers
        mockDisplay = new MockDisplay();
        mockPrimaryButton = new MockButton();
        mockSecondaryButton = new MockButton();
        mockHaptics = new MockHaptics();
        mockHttpClient = new MockHttpClient();
        mockPeerComms = new MockPeerComms();
        mockStorage = new MockStorage();
        lightManager = new LightManager(fakeLightStrip);
        wirelessManager = new WirelessManager(mockPeerComms, mockHttpClient);
    }

    ~MockDevice() {
        // Shutdown apps here (not in ~Device) because state dismount callbacks
        // may call virtual methods. During ~Device(), the vtable reverts to
        // Device's pure virtual definitions, causing SIGSEGV.
        shutdownApps();
        delete mockDisplay;
        delete mockPrimaryButton;
        delete mockSecondaryButton;
        delete mockHaptics;
        delete mockHttpClient;
        delete mockPeerComms;
        delete mockStorage;
        delete lightManager;
        delete wirelessManager;
    }

    // Device Methods
    MOCK_METHOD(int, begin, (), (override));
    MOCK_METHOD(void, setDeviceId, (const std::string&), (override));
    MOCK_METHOD(std::string, getDeviceId, (), (override));

    // Getters return mock instances
    Display* getDisplay() override { return mockDisplay; }
    Button* getPrimaryButton() override { return mockPrimaryButton; }
    Button* getSecondaryButton() override { return mockSecondaryButton; }
    Haptics* getHaptics() override { return mockHaptics; }
    HttpClientInterface* getHttpClient() override { return mockHttpClient; }
    PeerCommsInterface* getPeerComms() override { return mockPeerComms; }
    StorageInterface* getStorage() override { return mockStorage; }
    LightManager* getLightManager() override { return lightManager; }
    WirelessManager* getWirelessManager() override { return wirelessManager; }

    // DeviceSerial methods
    HWSerialWrapper* outputJack() override {
        return &outputJackSerial;
    }

    HWSerialWrapper* inputJack() override {
        return &inputJackSerial;
    }

    std::string getHead() {
        return primaryHead;
    }

    // Helper method to simulate serial receive for testing
    void simulateSerialReceive(const std::string& message) {
        if (outputJackSerial.stringCallback) {
            outputJackSerial.stringCallback(message);
        }
    }

    // Track callback clears for testing
    int serialCallbackClearCount = 0;

    void trackClearCallbacks() {
        serialCallbackClearCount++;
    }

    // Mock interface instances
    MockDisplay* mockDisplay;
    MockButton* mockPrimaryButton;
    MockButton* mockSecondaryButton;
    MockHaptics* mockHaptics;
    MockHttpClient* mockHttpClient;
    MockPeerComms* mockPeerComms;
    MockStorage* mockStorage;
    FakeLightStrip fakeLightStrip;
    LightManager* lightManager;
    WirelessManager* wirelessManager;

    FakeHWSerialWrapper outputJackSerial;
    FakeHWSerialWrapper inputJackSerial;
};
