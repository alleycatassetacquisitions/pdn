#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "apps/handshake/handshake.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "device/device-constants.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

inline Peer makeTestPeer(const uint8_t* mac, SerialIdentifier sid) {
    Peer p;
    std::copy(mac, mac + 6, p.macAddr.begin());
    p.sid = sid;
    return p;
}

// ============================================
// HandshakeWirelessManager Unit Tests
// ============================================

class HWMUnitTests : public testing::Test {
public:
    void SetUp() override {
        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        hwm.initialize(wirelessManager);
    }

    void TearDown() override {
        delete wirelessManager;
    }

    NiceMock<MockPeerComms> peerComms;
    MockHttpClient httpClient;
    WirelessManager* wirelessManager = nullptr;
    HandshakeWirelessManager hwm;
};

inline void hwmGetMacPeerReturnsNullWhenNotSet(HWMUnitTests* suite) {
    EXPECT_EQ(suite->hwm.getMacPeer(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(suite->hwm.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void hwmGetMacPeerReturnsCorrectMac(HWMUnitTests* suite) {
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    suite->hwm.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeTestPeer(mac, SerialIdentifier::OUTPUT_JACK));

    const Peer* result = suite->hwm.getMacPeer(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->macAddr[0], 0x11);
    EXPECT_EQ(result->macAddr[5], 0x66);

    EXPECT_EQ(suite->hwm.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void hwmRemoveMacPeerClearsEntry(HWMUnitTests* suite) {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->hwm.setMacPeer(SerialIdentifier::INPUT_JACK, makeTestPeer(mac, SerialIdentifier::INPUT_JACK));
    ASSERT_NE(suite->hwm.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);

    suite->hwm.removeMacPeer(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(suite->hwm.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void hwmSendPacketFailsWithNoPeer(HWMUnitTests* suite) {
    EXPECT_CALL(suite->peerComms, sendData(_, _, _, _)).Times(0);
    int result = suite->hwm.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(result, -1);
}

inline void hwmClearCallbacksRemovesAll(HWMUnitTests* suite) {
    bool inputFired = false;
    bool outputFired = false;

    suite->hwm.setPacketReceivedCallback([&](HandshakeCommand) { inputFired = true; }, SerialIdentifier::INPUT_JACK);
    suite->hwm.setPacketReceivedCallback([&](HandshakeCommand) { outputFired = true; }, SerialIdentifier::OUTPUT_JACK);

    suite->hwm.clearCallbacks();

    // Deliver a packet targeting INPUT_JACK (sent from OUTPUT_JACK)
    struct RawPacket { int jack; int command; } __attribute__((packed));
    uint8_t dummyMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    RawPacket pkt{ static_cast<int>(SerialIdentifier::OUTPUT_JACK), HSCommand::EXCHANGE_ID };
    suite->hwm.processHandshakeCommand(dummyMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));

    RawPacket pkt2{ static_cast<int>(SerialIdentifier::INPUT_JACK), HSCommand::EXCHANGE_ID };
    suite->hwm.processHandshakeCommand(dummyMac, reinterpret_cast<const uint8_t*>(&pkt2), sizeof(pkt2));

    EXPECT_FALSE(inputFired);
    EXPECT_FALSE(outputFired);
}

inline void hwmProcessRejectsNegativeCommand(HWMUnitTests* suite) {
    bool callbackFired = false;
    suite->hwm.setPacketReceivedCallback([&](HandshakeCommand) { callbackFired = true; }, SerialIdentifier::INPUT_JACK);

    struct RawPacket { int jack; int command; } __attribute__((packed));
    RawPacket pkt{ static_cast<int>(SerialIdentifier::OUTPUT_JACK), -1 };
    uint8_t dummyMac[6] = {};

    int result = suite->hwm.processHandshakeCommand(dummyMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    EXPECT_EQ(result, -1);
    EXPECT_FALSE(callbackFired);
}

// ============================================
// RemoteDeviceCoordinator Tests
// ============================================

class RDCTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(localMac));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kHandshakeCommand), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&capturedHandler),
                testing::SaveArg<2>(&capturedCtx)));

        device.rdcOverride = &rdc;
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    void deliverPacketViaRDC(int command, SerialIdentifier senderJack) {
        SerialIdentifier receivingJack = (senderJack == SerialIdentifier::OUTPUT_JACK)
            ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
        struct RawPacket { int sendingJack; int receivingJack; int command; } __attribute__((packed));
        RawPacket pkt{ static_cast<int>(senderJack), static_cast<int>(receivingJack), command };
        uint8_t dummyMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

        ASSERT_NE(capturedHandler, nullptr);
        capturedHandler(dummyMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt), capturedCtx);
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    PeerCommsInterface::PacketCallback capturedHandler = nullptr;
    void* capturedCtx = nullptr;
    uint8_t localMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
};

inline void rdcBothPortsDisconnectedOnInit(RDCTests* suite) {
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::DISCONNECTED);
}

inline void rdcOutputPortConnectingAfterMacReceived(RDCTests* suite) {
    // Simulate the foreign INPUT side sending its MAC over the output jack serial line
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF");

    // Sync to process the transition from OutputIdle -> OutputSendId
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTING);
}

inline void rdcOutputPortConnectedAfterHandshakeComplete(RDCTests* suite) {
    // Step 1: Output receives MAC over serial -> transitions to OutputSendId
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF");
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTING);

    // Step 2: Remote INPUT replies with EXCHANGE_ID -> Output transitions to Connected
    // OutputSendId is listening on OUTPUT_JACK callback.
    // A packet from the remote's INPUT_JACK gets inverted to OUTPUT_JACK on this device.
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
}

inline void rdcPortStateReturnsEmptyPeersWhenDisconnected(RDCTests* suite) {
    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);

    EXPECT_EQ(state.port, SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(state.status, PortStatus::DISCONNECTED);
    EXPECT_TRUE(state.peerMacAddresses.empty());
}

inline void rdcPortStateReturnsPeerWhenConnecting(RDCTests* suite) {
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF");
    suite->rdc.sync(&suite->device);

    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(state.status, PortStatus::CONNECTING);
    ASSERT_EQ(state.peerMacAddresses.size(), 1u);
    EXPECT_EQ(state.peerMacAddresses[0][0], 0xAA);
    EXPECT_EQ(state.peerMacAddresses[0][5], 0xFF);
}

inline void rdcInputPortDisconnectedIndependentOfOutputPort(RDCTests* suite) {
    // Move output port to CONNECTING
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF");
    suite->rdc.sync(&suite->device);

    // Input port should remain unaffected
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::DISCONNECTED);
    PortState inputState = suite->rdc.getPortState(SerialIdentifier::INPUT_JACK);
    EXPECT_TRUE(inputState.peerMacAddresses.empty());
}

// ============================================
// getPeerMac Tests
// ============================================

inline void rdcGetPeerMacReturnsNullWhenDisconnected(RDCTests* suite) {
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void rdcGetPeerMacReturnsMacWhenConnecting(RDCTests* suite) {
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF");
    suite->rdc.sync(&suite->device);

    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTING);

    const uint8_t* mac = suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(mac, nullptr);
    EXPECT_EQ(mac[0], 0xAA);
    EXPECT_EQ(mac[5], 0xFF);
}

inline void rdcGetPeerMacReturnsMacWhenConnected(RDCTests* suite) {
    // Complete the full handshake to reach CONNECTED
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    const uint8_t* mac = suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(mac, nullptr);
    EXPECT_EQ(mac[0], 0xAA);
    EXPECT_EQ(mac[5], 0xFF);

    // Other port remains null
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void rdcConnectedPortDisconnectsOnHeartbeatTimeout(RDCTests* suite) {
    // Complete the output-port handshake
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    // Let the heartbeat monitor timeout expire (firstHeartbeatTimeout = 400ms)
    suite->fakeClock->advance(500);
    suite->rdc.sync(&suite->device);

    // After timeout, the connected state should transition back to idle
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
}
