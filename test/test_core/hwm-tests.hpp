#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "apps/handshake/handshake.hpp"
#include "protocol-constants.hpp"
#include "device/drivers/peer-comms-types.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

inline Peer makeTestPeer(const uint8_t* mac, SerialIdentifier sid) {
    Peer p;
    std::copy(mac, mac + 6, p.macAddr.begin());
    p.sid = sid;
    p.deviceType = DeviceType::PDN;
    return p;
}

class HWMUnitTests : public testing::Test {
public:
    void SetUp() override {
        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        hwm.initialize(wirelessManager, DeviceType::PDN);
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

    struct RawPacket { int sendingJack; int receivingJack; int deviceType; int command; } __attribute__((packed));
    uint8_t dummyMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    RawPacket pkt{ static_cast<int>(SerialIdentifier::OUTPUT_JACK), static_cast<int>(SerialIdentifier::INPUT_JACK), 0, HSCommand::EXCHANGE_ID };
    suite->hwm.processHandshakeCommand(dummyMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));

    RawPacket pkt2{ static_cast<int>(SerialIdentifier::INPUT_JACK), static_cast<int>(SerialIdentifier::OUTPUT_JACK), 0, HSCommand::EXCHANGE_ID };
    suite->hwm.processHandshakeCommand(dummyMac, reinterpret_cast<const uint8_t*>(&pkt2), sizeof(pkt2));

    EXPECT_FALSE(inputFired);
    EXPECT_FALSE(outputFired);
}

inline void hwmSendPacketIncludesDeviceType(HWMUnitTests* suite) {
    uint8_t peerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    suite->hwm.initialize(suite->wirelessManager, DeviceType::FDN);
    suite->hwm.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeTestPeer(peerMac, SerialIdentifier::INPUT_JACK));

    const uint8_t* sentPayload = nullptr;
    size_t sentLen = 0;
    EXPECT_CALL(suite->peerComms, sendData(_, PktType::kHandshakeCommand, _, _))
        .WillOnce(Invoke([&](const uint8_t*, PktType, const uint8_t* data, size_t len) {
            sentPayload = data;
            sentLen = len;
            return 1;
        }));

    int result = suite->hwm.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    ASSERT_EQ(result, 1);
    ASSERT_NE(sentPayload, nullptr);
    ASSERT_EQ(sentLen, sizeof(int) * 4);

    struct RawPacket {
        int sendingJack;
        int receivingJack;
        int deviceType;
        int command;
    } __attribute__((packed));
    const RawPacket* packet = reinterpret_cast<const RawPacket*>(sentPayload);
    EXPECT_EQ(packet->deviceType, static_cast<int>(DeviceType::FDN));
    EXPECT_EQ(packet->command, HSCommand::EXCHANGE_ID);
}

inline void hwmProcessRejectsNegativeCommand(HWMUnitTests* suite) {
    bool callbackFired = false;
    suite->hwm.setPacketReceivedCallback([&](HandshakeCommand) { callbackFired = true; }, SerialIdentifier::INPUT_JACK);

    struct RawPacket { int sendingJack; int receivingJack; int deviceType; int command; } __attribute__((packed));
    RawPacket pkt{ static_cast<int>(SerialIdentifier::OUTPUT_JACK), static_cast<int>(SerialIdentifier::INPUT_JACK), 0, -1 };
    uint8_t dummyMac[6] = {};

    int result = suite->hwm.processHandshakeCommand(dummyMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    EXPECT_EQ(result, -1);
    EXPECT_FALSE(callbackFired);
}
