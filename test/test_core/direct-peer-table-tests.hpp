#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "wireless/direct-peer-table.hpp"

#include <optional>
#include <vector>

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

/// A DirectPeer with the given MAC, PDN device type, and jack id.
inline DirectPeer makeDirectPeer(const uint8_t* mac, SerialIdentifier sid) {
    DirectPeer p;
    std::copy(mac, mac + 6, p.macAddr.begin());
    p.sid = sid;
    p.deviceType = DeviceType::PDN;
    return p;
}

class DirectPeerTableTests : public testing::Test {
public:
    /// Wires the table to a WirelessManager over mocked radio comms.
    void SetUp() override {
        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        table.initialize(wirelessManager);
    }

    /// Releases the manager; the table holds no owned resources.
    void TearDown() override {
        delete wirelessManager;
        wirelessManager = nullptr;
    }

    NiceMock<MockPeerComms> peerComms;
    MockHttpClient httpClient;
    WirelessManager* wirelessManager = nullptr;
    DirectPeerTable table;
};

TEST_F(DirectPeerTableTests, getMacPeerReturnsNullWhenNotSet) {
    EXPECT_EQ(table.getMacPeer(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

TEST_F(DirectPeerTableTests, getMacPeerReturnsCorrectMac) {
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    table.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeDirectPeer(mac, SerialIdentifier::OUTPUT_JACK));

    const DirectPeer* result = table.getMacPeer(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->macAddr[0], 0x11);
    EXPECT_EQ(result->macAddr[5], 0x66);

    EXPECT_EQ(table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

TEST_F(DirectPeerTableTests, removeMacPeerClearsEntry) {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(mac, SerialIdentifier::INPUT_JACK));
    ASSERT_NE(table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);

    table.removeMacPeer(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

TEST_F(DirectPeerTableTests, setMacPeerRegistersEspNowSlot) {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    EXPECT_CALL(peerComms, addEspNowPeer(_)).Times(1);
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(mac, SerialIdentifier::INPUT_JACK));
}

TEST_F(DirectPeerTableTests, sameMacOnSecondJackDoesNotReRegister) {
    // A two-device ring presents one neighbor MAC on both jacks; the second
    // jack's connect must not register the ESP-NOW slot twice.
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    EXPECT_CALL(peerComms, addEspNowPeer(_)).Times(1);
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(mac, SerialIdentifier::INPUT_JACK));
    table.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeDirectPeer(mac, SerialIdentifier::OUTPUT_JACK));
}

TEST_F(DirectPeerTableTests, removeReleasesSlotWhenMacAbsentElsewhere) {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(mac, SerialIdentifier::INPUT_JACK));

    EXPECT_CALL(peerComms, removeEspNowPeer(_)).Times(1);
    table.removeMacPeer(SerialIdentifier::INPUT_JACK);
}

TEST_F(DirectPeerTableTests, twoDeviceRingKeepsSlotUntilBothJacksClear) {
    // The dereg guard: same MAC on both jacks (two-device ring). Dropping one
    // cable must NOT release the ESP-NOW slot the other jack still uses;
    // dropping the second releases it exactly once.
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(mac, SerialIdentifier::INPUT_JACK));
    table.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeDirectPeer(mac, SerialIdentifier::OUTPUT_JACK));

    EXPECT_CALL(peerComms, removeEspNowPeer(_)).Times(0);
    table.removeMacPeer(SerialIdentifier::INPUT_JACK);
    ::testing::Mock::VerifyAndClearExpectations(&peerComms);

    EXPECT_CALL(peerComms, removeEspNowPeer(_)).Times(1);
    table.removeMacPeer(SerialIdentifier::OUTPUT_JACK);
}

TEST_F(DirectPeerTableTests, selfMacIsRejected) {
    // Self-loopback (TRS echo) or a spoofed neighbor claiming our MAC must not
    // become a direct peer.
    static uint8_t selfMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    ON_CALL(peerComms, getMacAddress()).WillByDefault(Return(selfMac));

    EXPECT_CALL(peerComms, addEspNowPeer(_)).Times(0);
    EXPECT_FALSE(table.setMacPeer(SerialIdentifier::INPUT_JACK,
                                  makeDirectPeer(selfMac, SerialIdentifier::INPUT_JACK)));
    EXPECT_EQ(table.getMacPeer(SerialIdentifier::INPUT_JACK), nullptr);
}

TEST_F(DirectPeerTableTests, callbackFiresConnectAndDisconnectWithPostMutationState) {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    int connects = 0;
    int disconnects = 0;
    table.setPeerChangeCallback(
        [&](SerialIdentifier jack, std::optional<DirectPeer> previous,
            std::optional<DirectPeer> current) {
            if (current.has_value()) {
                ++connects;
                // macPeers is mutated before the callback: the entry is visible.
                EXPECT_NE(table.getMacPeer(jack), nullptr);
            } else {
                ++disconnects;
                EXPECT_EQ(previous->macAddr[0], 0x01);
                // Post-transition state: the dying entry is already gone.
                EXPECT_EQ(table.getMacPeer(jack), nullptr);
            }
        });

    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(mac, SerialIdentifier::INPUT_JACK));
    EXPECT_EQ(connects, 1);
    EXPECT_EQ(disconnects, 0);

    // Re-setting the same MAC on the same jack is not a transition.
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(mac, SerialIdentifier::INPUT_JACK));
    EXPECT_EQ(connects, 1);

    table.removeMacPeer(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(disconnects, 1);
}

TEST_F(DirectPeerTableTests, swapSurfacesAsDisconnectThenConnect) {
    // A different MAC on an occupied jack (old peer left, new one plugged in
    // within the silent-link window) must fire disconnect(old) then
    // connect(new), and swap the ESP-NOW slots accordingly.
    uint8_t macA[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x0A};
    uint8_t macB[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x0B};
    std::vector<uint8_t> transitions;  // 0 = disconnect, 1 = connect
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(macA, SerialIdentifier::INPUT_JACK));
    table.setPeerChangeCallback(
        [&](SerialIdentifier, std::optional<DirectPeer> previous,
            std::optional<DirectPeer> current) {
            transitions.push_back(current.has_value() ? 1 : 0);
            if (!current.has_value()) EXPECT_EQ(previous->macAddr[5], 0x0A);
            if (current.has_value()) EXPECT_EQ(current->macAddr[5], 0x0B);
        });

    EXPECT_CALL(peerComms, removeEspNowPeer(_)).Times(1);  // macA released
    EXPECT_CALL(peerComms, addEspNowPeer(_)).Times(1);     // macB registered
    table.setMacPeer(SerialIdentifier::INPUT_JACK, makeDirectPeer(macB, SerialIdentifier::INPUT_JACK));

    ASSERT_EQ(transitions.size(), 2u);
    EXPECT_EQ(transitions[0], 0);
    EXPECT_EQ(transitions[1], 1);
}
