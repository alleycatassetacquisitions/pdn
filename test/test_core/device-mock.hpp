//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include <gmock/gmock.h>
#include "device/pdn.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/display.hpp"
#include "device/drivers/button.hpp"
#include "device/drivers/haptics.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/light-manager.hpp"
#include "wireless/quickdraw-packet.hpp"
#include "game/chain-duel-manager.hpp"
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

    /** Routes RX bytes to the byte callback; see HWSerialWrapper. */
    void setByteCallback(const SerialByteCallback& callback) override {
        byteCallback = callback;
    }

    deque<char> msgQueue;
    SerialStringCallback stringCallback;
    SerialByteCallback byteCallback;
};

class FakeDevice : public Device {
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
    MOCK_METHOD(int, getTextWidth, (const char*), (override));
    MOCK_METHOD(int, getWidth, (), (override));
    MOCK_METHOD(Display*, whiteScreen, (), (override));
    MOCK_METHOD(Display*, whiteScreenLeftHalf, (), (override));
    MOCK_METHOD(Display*, whiteScreenRightHalf, (), (override));
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
    MOCK_METHOD(void, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(void, setPeerCommsState, (PeerCommsState), (override));
    MOCK_METHOD(PeerCommsState, getPeerCommsState, (), (override));
    /** Mocked so tests can capture the handler and replay a radio SEND_SUCCESS;
     *  on channels with no reply packet that result IS the delivery signal. */
    MOCK_METHOD(void, setSendStatusHandler, (PktType, SendStatusCallback, void*), (override));
    /** Paired with setSendStatusHandler; dropped when an owner goes away. */
    MOCK_METHOD(void, clearSendStatusHandler, (PktType), (override));
};

class MockStorage : public StorageInterface {
public:
    MOCK_METHOD(size_t, write, (const std::string&, const std::string&, const std::string&), (override));
    MOCK_METHOD(std::string, read, (const std::string&, const std::string&, const std::string&), (override));
    MOCK_METHOD(bool, remove, (const std::string&, const std::string&), (override));
    MOCK_METHOD(bool, clear, (const std::string&), (override));
    MOCK_METHOD(void, end, (), (override));
    MOCK_METHOD(uint8_t, readUChar, (const std::string&, const std::string&, uint8_t), (override));
    MOCK_METHOD(size_t, writeUChar, (const std::string&, const std::string&, uint8_t), (override));
};

class FakeRemoteDeviceCoordinator : public RemoteDeviceCoordinator {
public:
    /** On a ring whenever a head is set. Off one by default, as the base reports. */
    bool isInRing() const override { return ringHeadMac != std::array<uint8_t, 6>{}; }
    /** The head a test put this device under; nullptr off a ring. */
    const uint8_t* getHeadMac() const override {
        return ringHeadMac == std::array<uint8_t, 6>{} ? nullptr : ringHeadMac.data();
    }
    std::array<uint8_t, 6> ringHeadMac{};

    void setPortStatus(SerialIdentifier id, PortStatus status) {
        if (id == SerialIdentifier::OUTPUT_JACK) outputStatus = status;
        else if (id == SerialIdentifier::INPUT_JACK) inputStatus = status;
    }

    PortStatus getPortStatus(SerialIdentifier id) override {
        if (id == SerialIdentifier::OUTPUT_JACK) return outputStatus;
        if (id == SerialIdentifier::INPUT_JACK) return inputStatus;
        return PortStatus::DISCONNECTED;
    }

    void setPeerDeviceType(SerialIdentifier id, DeviceType type) {
        if (id == SerialIdentifier::OUTPUT_JACK) outputDeviceType = type;
        else if (id == SerialIdentifier::INPUT_JACK) inputDeviceType = type;
    }

    DeviceType getPeerDeviceType(SerialIdentifier id) const override {
        if (id == SerialIdentifier::OUTPUT_JACK) return outputDeviceType;
        if (id == SerialIdentifier::INPUT_JACK) return inputDeviceType;
        return DeviceType::UNKNOWN;
    }

    /** Seeds a PDN peer profile on `id` carrying `gameRole`. */
    void setPeerRole(SerialIdentifier id, bool isHunter) {
        setPeerDeviceType(id, DeviceType::PDN);
        PlayerProfile profile{};
        profile.gameRole = isHunter ? 1 : 0;
        if (id == SerialIdentifier::OUTPUT_JACK) outputProfile = profile;
        else if (id == SerialIdentifier::INPUT_JACK) inputProfile = profile;
    }

    const uint8_t* getPeerProfile(SerialIdentifier id, size_t& length) const override {
        const std::optional<PlayerProfile>* slot = nullptr;
        if (id == SerialIdentifier::OUTPUT_JACK) slot = &outputProfile;
        else if (id == SerialIdentifier::INPUT_JACK) slot = &inputProfile;
        if (slot == nullptr || !slot->has_value()) {
            length = 0;
            return nullptr;
        }
        length = sizeof(PlayerProfile);
        return reinterpret_cast<const uint8_t*>(&slot->value());
    }

    // Stubbable direct peer MACs — tests that exercise peer-MAC gating set
    // these; getPeerMac returns a pointer into the stored array or nullptr.
    void setPeerMac(SerialIdentifier id, const uint8_t* mac) {
        if (id == SerialIdentifier::OUTPUT_JACK) {
            outputPeerSet = (mac != nullptr);
            if (mac) memcpy(outputPeerMac, mac, 6);
        } else if (id == SerialIdentifier::INPUT_JACK) {
            inputPeerSet = (mac != nullptr);
            if (mac) memcpy(inputPeerMac, mac, 6);
        }
    }

    const uint8_t* getPeerMac(SerialIdentifier id) const override {
        if (id == SerialIdentifier::OUTPUT_JACK && outputPeerSet) return outputPeerMac;
        if (id == SerialIdentifier::INPUT_JACK && inputPeerSet) return inputPeerMac;
        return nullptr;
    }

    // Production RDC reads its per-jack link machines. The fake stubs peer MACs
    // directly without standing any up, so iterate the ports here.
    bool isDirectPeer(const uint8_t* mac) const override {
        if (!mac) return false;
        for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
            const uint8_t* peer = getPeerMac(port);
            if (peer && memcmp(peer, mac, 6) == 0) return true;
        }
        return false;
    }

private:
    PortStatus outputStatus = PortStatus::DISCONNECTED;
    PortStatus inputStatus = PortStatus::DISCONNECTED;
    DeviceType outputDeviceType = DeviceType::UNKNOWN;
    DeviceType inputDeviceType = DeviceType::UNKNOWN;
    std::optional<PlayerProfile> outputProfile;
    std::optional<PlayerProfile> inputProfile;
    uint8_t outputPeerMac[6] = {};
    uint8_t inputPeerMac[6] = {};
    bool outputPeerSet = false;
    bool inputPeerSet = false;
};

// Stand-in RDC reporting a latched ring plus a head roster without driving the
// HELLO stack. Only the chain surface ShootoutManager reads is overridden.
class FakeRingRemoteDeviceCoordinator : public RemoteDeviceCoordinator {
public:
    /** The role this stand-in reports; RING by default. */
    ChainRole getChainRole() const override { return chainRole; }
    /** The roster this stand-in serves, as a real head's RDC would. */
    std::vector<std::array<uint8_t, 6>> getChainMembers() const override { return chainMembers; }
    /**
     * Membership is broader than the RING role: a device relaying another head's
     * closure sits on a live loop with no latch of its own.
     */
    bool isInRing() const override {
        return chainRole == ChainRole::RING || headMac != std::array<uint8_t, 6>{};
    }

    /** Stubbed upstream head MAC, or nullptr when unset. */
    const uint8_t* getHeadMac() const override {
        return headMac == std::array<uint8_t, 6>{} ? nullptr : headMac.data();
    }

    ChainRole chainRole = ChainRole::RING;
    std::vector<std::array<uint8_t, 6>> chainMembers;
    std::array<uint8_t, 6> headMac{};
};

class QuickdrawRadioTap {
public:
    /** Call after any wildcard sendData ON_CALL and before constructing MatchManager. */
    void attach(MockPeerComms* radio) {
        ON_CALL(*radio, setPacketHandler(PktType::kQuickdrawCommand, testing::_, testing::_))
            .WillByDefault(testing::Invoke(
                [this](PktType, PeerCommsInterface::PacketCallback callback, void* ctx) {
                    receiveHandler = callback;
                    receiveContext = ctx;
                }));
        ON_CALL(*radio, sendData(testing::_, PktType::kQuickdrawCommand, testing::_, testing::_))
            .WillByDefault(testing::DoAll(
                testing::Invoke([this](const uint8_t*, PktType, const uint8_t* data,
                                       const size_t len) {
                    if (len != sizeof(QuickdrawPacket)) return;
                    QuickdrawPacket p{};
                    memcpy(&p, data, sizeof(p));
                    sentFrames.push_back(p);
                }),
                testing::Return(1)));
    }

    /** The exact frame this radio last sent, for the peer's tap to replay. */
    const QuickdrawPacket* lastFrame() const {
        return sentFrames.empty() ? nullptr : &sentFrames.back();
    }

    /** Delivers frame from senderMac into this tap's own receiveHandler. */
    void deliverFrameFrom(const uint8_t* senderMac, const QuickdrawPacket* frame) {
        if (frame == nullptr) return;
        deliverBytesTo(senderMac, reinterpret_cast<const uint8_t*>(frame),
                       sizeof(*frame));
    }

    /** Feeds raw wire bytes into this radio's manager as the driver would. */
    void deliverBytesTo(const uint8_t* senderMac, const uint8_t* data, size_t len) {
        if (receiveHandler != nullptr) {
            receiveHandler(senderMac, data, len, receiveContext);
        }
    }

    std::vector<QuickdrawPacket> sentFrames;

private:
    PeerCommsInterface::PacketCallback receiveHandler = nullptr;
    void* receiveContext = nullptr;
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

class MockDevice : public PDN {
public:
    static constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    MockDevice() : PDN() {
        // Initialize mock pointers
        mockDisplay = new MockDisplay();
        mockPrimaryButton = new MockButton();
        mockSecondaryButton = new MockButton();
        mockHaptics = new MockHaptics();
        mockHttpClient = new MockHttpClient();
        mockPeerComms = new MockPeerComms();
        mockStorage = new MockStorage();
        lightManager = new LightManager(fakeLightStrip);
        serialManager = new SerialManager(&outputJackSerial, &inputJackSerial);
        wirelessManager = new WirelessManager(mockPeerComms, mockHttpClient);
        // The real driver registers a permanent broadcast peer at radio init and
        // hands its address back here; broadcast fan-out is unreachable without it.
        ON_CALL(*mockPeerComms, getGlobalBroadcastAddress())
            .WillByDefault(testing::Return(BROADCAST_MAC));
    }

    ~MockDevice() {
        delete mockDisplay;
        delete mockPrimaryButton;
        delete mockSecondaryButton;
        delete mockHaptics;
        delete mockHttpClient;
        delete mockPeerComms;
        delete mockStorage;
        delete lightManager;
        delete serialManager;
        delete wirelessManager;
    }

    // Device Methods
    MOCK_METHOD(int, begin, (), (override));
    MOCK_METHOD(void, setDeviceId, (const std::string&), (override));
    MOCK_METHOD(std::string, getDeviceId, (), (override));
    DeviceType getDeviceType() override { return DeviceType::PDN; }
    void loop() override { Device::loop(); }

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
    SerialManager* getSerialManager() override { return serialManager; }
    RemoteDeviceCoordinator* getRemoteDeviceCoordinator() override { return &fakeRemoteDeviceCoordinator; }

    std::string getHead() {
        return serialManager->getOutputHead();
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
    SerialManager* serialManager;
    WirelessManager* wirelessManager;

    FakeHWSerialWrapper outputJackSerial;
    FakeHWSerialWrapper inputJackSerial;
    FakeRemoteDeviceCoordinator fakeRemoteDeviceCoordinator;
};
