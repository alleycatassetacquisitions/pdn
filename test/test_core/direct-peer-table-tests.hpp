#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "wireless/direct-peer-table.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

inline DirectPeer makeTestPeer(const uint8_t* mac, SerialIdentifier sid) {
    DirectPeer p;
    std::copy(mac, mac + 6, p.macAddr.begin());
    p.sid = sid;
    p.deviceType = DeviceType::PDN;
    return p;
}

class DirectPeerTableTests : public testing::Test {
public:
    void SetUp() override {
        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        table.initialize(wirelessManager);
    }

    void TearDown() override {
        delete wirelessManager;
    }

    NiceMock<MockPeerComms> peerComms;
    MockHttpClient httpClient;
    WirelessManager* wirelessManager = nullptr;
    DirectPeerTable table;
};

inline void directPeerTableGetMacPeerReturnsNullWhenNotSet(DirectPeerTableTests* suite) {
    EXPECT_EQ(suite->table.getMacPeer(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(suite->table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void directPeerTableGetMacPeerReturnsCorrectMac(DirectPeerTableTests* suite) {
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    suite->table.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeTestPeer(mac, SerialIdentifier::OUTPUT_JACK));

    const DirectPeer* result = suite->table.getMacPeer(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->macAddr[0], 0x11);
    EXPECT_EQ(result->macAddr[5], 0x66);

    EXPECT_EQ(suite->table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void directPeerTableRemoveMacPeerClearsEntry(DirectPeerTableTests* suite) {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->table.setMacPeer(SerialIdentifier::INPUT_JACK, makeTestPeer(mac, SerialIdentifier::INPUT_JACK));
    ASSERT_NE(suite->table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);

    suite->table.removeMacPeer(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(suite->table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}
