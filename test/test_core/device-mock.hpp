//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include "game/match-manager.hpp"

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

    /// Routes RX bytes to the byte callback; see HWSerialWrapper.
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
    uint8_t outputPeerMac[6] = {};
    uint8_t inputPeerMac[6] = {};
    bool outputPeerSet = false;
    bool inputPeerSet = false;
};

// Stand-in RDC reporting a latched ring plus a head roster without driving the
// HELLO stack. Only the chain surface ShootoutManager reads is overridden.
class FakeRingRemoteDeviceCoordinator : public RemoteDeviceCoordinator {
public:
    /// The role this stand-in reports; RING by default.
    ChainRole getChainRole() const override { return chainRole; }
    /// The roster this stand-in serves, as a real head's RDC would.
    std::vector<std::array<uint8_t, 6>> getChainMembers() const override { return chainMembers; }
    /// Membership is broader than the RING role: a device relaying another head's
    /// closure sits on a live loop with no latch of its own.
    bool isInRing() const override {
        return chainRole == ChainRole::RING || relayedMember;
    }

    ChainRole chainRole = ChainRole::RING;
    bool relayedMember = false;
    std::vector<std::array<uint8_t, 6>> chainMembers;
};

// Captures kQuickdrawCommand frames as MatchManager hands them to the radio, and
// replays one into another manager. Both directions run the real wire format, so
// the encode and decode the duel depends on are still exercised — the send half
// now goes through the channel and the radio rather than a stubbed method.
class FakeQuickdrawWirelessManager {
public:
    /// Captures every quickdraw frame this radio is asked to send.
    void attach(MockPeerComms* radio) {
        ON_CALL(*radio, sendData(testing::_, PktType::kQuickdrawCommand, testing::_, testing::_))
            .WillByDefault(testing::DoAll(
                testing::Invoke([this](const uint8_t* dst, PktType, const uint8_t* data,
                                       const size_t len) {
                    if (len != sizeof(QuickdrawPacket)) return;
                    QuickdrawPacket p{};
                    memcpy(&p, data, sizeof(p));
                    // Copied out first: emplace_back forwards by reference and a
                    // packed member has no address a reference may bind to.
                    const int command = p.command;
                    const long drawTime = p.playerDrawTime;
                    const bool isHunter = p.isHunter;
                    sentCommands.emplace_back(dst, command, p.matchId, p.playerId,
                                              drawTime, isHunter);
                }),
                testing::Return(1)));
    }

    /// Round-trips the most recent capture through the wire format and hands it
    /// to `recipient` as if it had arrived from `senderMac`.
    void deliverLastTo(MatchManager* recipient, const uint8_t* senderMac) {
        if (sentCommands.empty() || recipient == nullptr) return;
        const QuickdrawCommand& cmd = sentCommands.back();

        QuickdrawPacket pkt = {};
        memcpy(pkt.matchId, cmd.matchId, sizeof(pkt.matchId));
        memcpy(pkt.playerId, cmd.playerId, sizeof(pkt.playerId));
        pkt.isHunter = cmd.isHunter;
        pkt.playerDrawTime = cmd.playerDrawTime;
        pkt.command = cmd.command;

        deliverBytesTo(recipient, senderMac, reinterpret_cast<const uint8_t*>(&pkt),
                       sizeof(pkt));
    }

    /// Observer for every command this tap decodes, so a test can inspect the
    /// wire round-trip without standing up a manager.
    void setPacketReceivedCallback(std::function<void(const QuickdrawCommand&)> cb) {
        packetReceivedCallback = std::move(cb);
    }

    /// Drops the decode observer.
    void clearCallbacks() { packetReceivedCallback = nullptr; }

    /// Feeds raw wire bytes to `recipient` as if the radio had delivered them.
    /// `recipient` may be null when only the decode is under test.
    void deliverBytesTo(MatchManager* recipient, const uint8_t* senderMac,
                        const uint8_t* data, size_t len) {
        if (len != sizeof(QuickdrawPacket)) return;
        QuickdrawPacket p{};
        memcpy(&p, data, sizeof(p));
        const int command = p.command;
        const long drawTime = p.playerDrawTime;
        const bool isHunter = p.isHunter;
        QuickdrawCommand decoded(senderMac, command, p.matchId, p.playerId, drawTime,
                                 isHunter);
        if (packetReceivedCallback) packetReceivedCallback(decoded);
        if (recipient != nullptr) recipient->listenForMatchEvents(decoded);
    }

    std::vector<QuickdrawCommand> sentCommands;

private:
    std::function<void(const QuickdrawCommand&)> packetReceivedCallback;
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
