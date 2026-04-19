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
#include "game/player.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

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
        ON_CALL(*device.mockPeerComms, addEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, removeEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, getPeerCommsState()).WillByDefault(Return(PeerCommsState::CONNECTED));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kHandshakeCommand), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&capturedHandler),
                testing::SaveArg<2>(&capturedCtx)));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kChainAnnouncement), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&capturedChainHandler),
                testing::SaveArg<2>(&capturedChainCtx)));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kChainAnnouncementAck), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&capturedAckHandler),
                testing::SaveArg<2>(&capturedAckCtx)));

        rdc.initialize(device.wirelessManager, device.serialManager, &device);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    void deliverPacketViaRDC(int command, SerialIdentifier senderJack, int deviceType = 0) {
        SerialIdentifier receivingJack = (senderJack == SerialIdentifier::OUTPUT_JACK)
            ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
        struct RawPacket { int sendingJack; int receivingJack; int deviceType; int command; } __attribute__((packed));
        RawPacket pkt{ static_cast<int>(senderJack), static_cast<int>(receivingJack), deviceType, command };
        uint8_t dummyMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

        ASSERT_NE(capturedHandler, nullptr);
        capturedHandler(dummyMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt), capturedCtx);
    }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    PeerCommsInterface::PacketCallback capturedHandler = nullptr;
    void* capturedCtx = nullptr;
    PeerCommsInterface::PacketCallback capturedChainHandler = nullptr;
    void* capturedChainCtx = nullptr;
    PeerCommsInterface::PacketCallback capturedAckHandler = nullptr;
    void* capturedAckCtx = nullptr;
    uint8_t localMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
};

// Default state: both ports disconnected, all getters return empty/null/unknown
inline void rdcDefaultStateIsDisconnectedOnAllPorts(RDCTests* suite) {
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::DISCONNECTED);

    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(state.status, PortStatus::DISCONNECTED);
    EXPECT_TRUE(state.peerMacAddresses.empty());

    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);

    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::UNKNOWN);
    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::INPUT_JACK), DeviceType::UNKNOWN);
}

// Output port connection lifecycle: disconnected → connecting → connected,
// with port state, peer MAC, and device type correct at each stage.
// Input port stays independent throughout.
inline void rdcOutputPortConnectionLifecycle(RDCTests* suite) {
    // MAC received over serial → CONNECTING
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTING);
    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::PDN);
    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::INPUT_JACK), DeviceType::UNKNOWN);

    PortState connectingState = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    ASSERT_EQ(connectingState.peerMacAddresses.size(), 1u);
    EXPECT_EQ(connectingState.peerMacAddresses[0][0], 0xAA);

    const uint8_t* mac = suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(mac, nullptr);
    EXPECT_EQ(mac[0], 0xAA);
    EXPECT_EQ(mac[5], 0xFF);

    // Input port unaffected
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::DISCONNECTED);
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);

    // EXCHANGE_ID reply → CONNECTED
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
    mac = suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(mac, nullptr);
    EXPECT_EQ(mac[0], 0xAA);

    // Input still unaffected
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);
}

inline void rdcConnectedPortDisconnectsOnHeartbeatTimeout(RDCTests* suite) {
    // Complete the output-port handshake
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
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

// ============================================
// Daisy chain tracking
// ============================================

inline void rdcDuplicateAnnouncementDoesNotFireCallback(RDCTests* suite) {
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);

    uint8_t directPeerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::vector<std::array<uint8_t, 6>> peers = {
        {0x33, 0x33, 0x33, 0x33, 0x33, 0x33}
    };
    suite->rdc.onChainAnnouncementReceived(directPeerMac, SerialIdentifier::OUTPUT_JACK, peers);

    // First announcement set the state. Now hook the callback and send the same again.
    int callbackCount = 0;
    suite->rdc.setChainChangeCallback([&]() { callbackCount++; });

    suite->rdc.onChainAnnouncementReceived(directPeerMac, SerialIdentifier::OUTPUT_JACK, peers);

    EXPECT_EQ(callbackCount, 0);
}

inline void rdcDirectPeerRegistrationEmitsBackwardAnnouncement(RDCTests* suite) {
    // Bring up OUTPUT port first
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    // Hook callback BEFORE bringing up INPUT port
    int emitCount = 0;
    suite->rdc.setAnnouncementEmitCallback(
        [&](const uint8_t*, uint8_t, const std::vector<std::array<uint8_t, 6>>&) { emitCount++; });

    // Bring up INPUT port — this should emit BOTH forward (to OUTPUT's peer)
    // AND backward (to the new INPUT peer about OUTPUT's chain)
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(emitCount, 2);
}

inline void rdcDirectPeerDropEmitsAnnouncement(RDCTests* suite) {
    // Bring up both ports
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::CONNECTED);

    // Hook callback AFTER both ports are up so registration emits don't pollute the count
    int emitCount = 0;
    suite->rdc.setAnnouncementEmitCallback(
        [&](const uint8_t*, uint8_t, const std::vector<std::array<uint8_t, 6>>&) { emitCount++; });

    // Drop OUTPUT only — deliver NOTIFY_DISCONNECT to OUTPUT (sender = INPUT_JACK on the peer)
    suite->deliverPacketViaRDC(HSCommand::NOTIFY_DISCONNECT, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::CONNECTED);

    EXPECT_GT(emitCount, 0);
}

inline void rdcAnnouncementAbandonedAfterMaxRetries(RDCTests* suite) {
    int chainAnnouncementSendCount = 0;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainAnnouncement, _, _))
        .WillByDefault(testing::DoAll(
            testing::InvokeWithoutArgs([&]() { chainAnnouncementSendCount++; }),
            Return(1)));

    // Bring up both ports → 2 initial emits, 2 pending
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    chainAnnouncementSendCount = 0;

    // Run many retransmit cycles, refreshing heartbeats so the connection
    // doesn't drop. With max_retries=3 and exponential backoff (100, 200,
    // 400ms), each pending retransmits at most 3 times then abandons.
    for (int i = 0; i < 20; i++) {
        suite->device.outputJackSerial.stringCallback(SERIAL_HEARTBEAT);
        suite->device.inputJackSerial.stringCallback(SERIAL_HEARTBEAT);
        suite->fakeClock->advance(110);
        suite->rdc.sync(&suite->device);
    }

    // Sanity: at least some retransmits fired across both pending ports.
    EXPECT_GT(chainAnnouncementSendCount, 3);
    // 2 pending * 3 retries = 6 retransmits maximum
    EXPECT_LE(chainAnnouncementSendCount, 6);
}

inline void rdcAckedAnnouncementDoesNotRetransmit(RDCTests* suite) {
    std::vector<uint8_t> sentAnnouncementIds;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainAnnouncement, _, _))
        .WillByDefault(testing::DoAll(
            testing::WithArg<2>(testing::Invoke([&](const uint8_t* data) {
                sentAnnouncementIds.push_back(data[0]);
            })),
            Return(1)));
    ASSERT_NE(suite->capturedAckHandler, nullptr);

    // Bring up both ports → emits, pending state set
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);

    ASSERT_GE(sentAnnouncementIds.size(), 1u);  // at least one emit happened

    // Deliver ACK for the most recent pending (the latest id per port is what's active)
    uint8_t fromMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    for (uint8_t id : sentAnnouncementIds) {
        suite->capturedAckHandler(fromMac, &id, 1, suite->capturedAckCtx);
    }

    sentAnnouncementIds.clear();
    suite->fakeClock->advance(150);
    suite->rdc.sync(&suite->device);

    // No retransmits should have fired for any acked pending
    EXPECT_EQ(sentAnnouncementIds.size(), 0u);
}

inline void rdcChainAnnouncementPacketHandlerUpdatesDaisyChain(RDCTests* suite) {
    // Bring up OUTPUT port
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
    ASSERT_NE(suite->capturedChainHandler, nullptr);

    // Build a chain announcement packet from the direct peer.
    // Wire: [id, count, mac(6)]
    uint8_t fromMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t buf[8];
    buf[0] = 0x01;  // announcement_id
    buf[1] = 1;     // peer count
    buf[2] = 0x55; buf[3] = 0x55; buf[4] = 0x55;
    buf[5] = 0x55; buf[6] = 0x55; buf[7] = 0x55;

    suite->capturedChainHandler(fromMac, buf, sizeof(buf), suite->capturedChainCtx);

    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(state.peerMacAddresses.size(), 2u);  // direct + 1 daisy
}

inline void rdcMidChainEmitsForwardAndBackward(RDCTests* suite) {
    // Bring up OUTPUT port
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    // Bring up INPUT port via wireless EXCHANGE_ID exchange
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::CONNECTED);

    // Track all emissions
    std::vector<std::array<uint8_t, 6>> emittedTos;
    suite->rdc.setAnnouncementEmitCallback(
        [&](const uint8_t* toMac, uint8_t, const std::vector<std::array<uint8_t, 6>>&) {
            std::array<uint8_t, 6> mac;
            std::copy(toMac, toMac + 6, mac.begin());
            emittedTos.push_back(mac);
        });

    // Receive an announcement on OUTPUT
    uint8_t outputDirectMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::vector<std::array<uint8_t, 6>> incoming = {
        {0x33, 0x33, 0x33, 0x33, 0x33, 0x33}
    };
    suite->rdc.onChainAnnouncementReceived(
        outputDirectMac, SerialIdentifier::OUTPUT_JACK, incoming);

    // Should emit forward to INPUT's direct peer (the dummyMac AA:BB:CC:DD:EE:FF used by deliverPacketViaRDC)
    EXPECT_GE(emittedTos.size(), 1u);
}

inline void rdcDoesNotEmitWhenOtherPortDisconnected(RDCTests* suite) {
    // Only OUTPUT is connected — INPUT has no direct peer
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);

    int emitCount = 0;
    suite->rdc.setAnnouncementEmitCallback(
        [&](const uint8_t*, uint8_t, const std::vector<std::array<uint8_t, 6>>&) { emitCount++; });

    uint8_t outputDirectPeerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::vector<std::array<uint8_t, 6>> incoming = {
        {0x33, 0x33, 0x33, 0x33, 0x33, 0x33}
    };
    suite->rdc.onChainAnnouncementReceived(
        outputDirectPeerMac, SerialIdentifier::OUTPUT_JACK, incoming);

    // No emission because INPUT has no direct peer to forward to
    EXPECT_EQ(emitCount, 0);
}

inline void rdcChainChangeCallbackFiresOnDaisyAdded(RDCTests* suite) {
    // Set up: OUTPUT port connected
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);

    int callbackFireCount = 0;
    suite->rdc.setChainChangeCallback([&]() { callbackFireCount++; });

    // Receiving an announcement should fire the callback
    uint8_t directPeerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::vector<std::array<uint8_t, 6>> announcedPeers = {
        {0x22, 0x22, 0x22, 0x22, 0x22, 0x22}
    };
    suite->rdc.onChainAnnouncementReceived(
        directPeerMac, SerialIdentifier::OUTPUT_JACK, announcedPeers);

    EXPECT_EQ(callbackFireCount, 1);
}

inline void rdcIgnoresAnnouncementFromNonDirectPeer(RDCTests* suite) {
    // Set up: OUTPUT port connected to peer AA:BB:CC:DD:EE:FF
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    // Announcement from a different MAC (not our direct peer) should be ignored
    uint8_t strangerMac[6] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
    std::vector<std::array<uint8_t, 6>> announcedPeers = {
        {0x22, 0x22, 0x22, 0x22, 0x22, 0x22}
    };

    suite->rdc.onChainAnnouncementReceived(
        strangerMac, SerialIdentifier::OUTPUT_JACK, announcedPeers);

    // No daisy-chained peers should have been added
    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(state.peerMacAddresses.size(), 1u);  // only direct peer
    EXPECT_EQ(state.status, PortStatus::CONNECTED);
}

inline void rdcDisconnectWipesDaisyChainedPeers(RDCTests* suite) {
    // Set up: OUTPUT port connected with a daisy-chained peer
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);

    uint8_t directPeerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::vector<std::array<uint8_t, 6>> announcedPeers = {
        {0x22, 0x22, 0x22, 0x22, 0x22, 0x22},
        {0x33, 0x33, 0x33, 0x33, 0x33, 0x33},
    };
    suite->rdc.onChainAnnouncementReceived(
        directPeerMac, SerialIdentifier::OUTPUT_JACK, announcedPeers);

    ASSERT_EQ(suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK).peerMacAddresses.size(), 3u);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DAISY_CHAINED);

    // Heartbeat monitor timeout expires → handshake drops → port disconnects
    suite->fakeClock->advance(500);
    suite->rdc.sync(&suite->device);
    suite->rdc.sync(&suite->device);

    // Both direct and daisy-chained peers should be cleared
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    EXPECT_TRUE(state.peerMacAddresses.empty());
}

inline void rdcChainAnnouncementFiltersSelfMac(RDCTests* suite) {
    // Set up: OUTPUT port connected to a direct peer
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    // Direct peer announces a chain including this device's own MAC (ring)
    uint8_t directPeerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::vector<std::array<uint8_t, 6>> announcedPeers = {
        {0x22, 0x22, 0x22, 0x22, 0x22, 0x22},
        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},  // our own MAC (localMac)
        {0x33, 0x33, 0x33, 0x33, 0x33, 0x33},
    };

    suite->rdc.onChainAnnouncementReceived(
        directPeerMac,
        SerialIdentifier::OUTPUT_JACK,
        announcedPeers);

    // Self-MAC must be filtered out — only the two other peers remain as daisy-chained
    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    ASSERT_EQ(state.peerMacAddresses.size(), 3u);  // direct + 2 daisy-chained
    EXPECT_EQ(state.peerMacAddresses[0][0], 0xAA);  // direct peer
    EXPECT_EQ(state.peerMacAddresses[1][0], 0x22);  // first non-self daisy
    EXPECT_EQ(state.peerMacAddresses[2][0], 0x33);  // second non-self daisy
}

// Daisy-chained peer list is capped at kMaxChainPeersPerPort (18). An
// announcement naming more peers than the cap is silently truncated — the
// excess peers are invisible to this device. Guards the ESP-NOW peer-table
// limit (20 on ESP32-S3).
inline void rdcDaisyChainCappedAtMaxPeers(RDCTests* suite) {
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    uint8_t directPeerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::vector<std::array<uint8_t, 6>> announced;
    for (uint8_t i = 0; i < 25; ++i) {
        announced.push_back({static_cast<uint8_t>(0x30 + i), 0, 0, 0, 0, 0});
    }

    suite->rdc.onChainAnnouncementReceived(
        directPeerMac, SerialIdentifier::OUTPUT_JACK, announced);

    // direct peer + at most 18 daisy = 19
    PortState state = suite->rdc.getPortState(SerialIdentifier::OUTPUT_JACK);
    EXPECT_LE(state.peerMacAddresses.size(), 19u);
    EXPECT_GE(state.peerMacAddresses.size(), 2u);
}

