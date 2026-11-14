#pragma once

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "duel-session-tests.hpp"
#include "utility-tests.hpp"
#include "wireless/node-network-wireless-manager.hpp"

/** Captures outgoing packets instead of touching ESP-NOW. */
class FakeNodeNetworkWirelessManager : public NodeNetworkWirelessManager {
public:
    struct SentPacket {
        std::array<uint8_t, 6> destinationMac;
        NodeNetworkPacket packet;
    };

    int sendPacket(const uint8_t* destinationMac, const NodeNetworkPacket& packet) override {
        SentPacket sentPacket;
        memcpy(sentPacket.destinationMac.data(), destinationMac, 6);
        sentPacket.packet = packet;
        sent.push_back(sentPacket);
        return 0;
    }

    /** @return how many captured packets carry @p command. */
    size_t countSent(NodeNetworkCommand command) const {
        size_t count = 0;
        for (const SentPacket& entry : sent) {
            if (entry.packet.command == static_cast<uint8_t>(command)) {
                count++;
            }
        }
        return count;
    }

    std::vector<SentPacket> sent;
};

inline NodeNetworkPacket makeNodeNetworkPacket(NodeNetworkCommand command, bool senderIsInitiator) {
    NodeNetworkPacket packet;
    packet.command = static_cast<uint8_t>(command);
    packet.senderIsInitiator = senderIsInitiator ? 1 : 0;
    nodeNetworkPacketSetUuid(packet.matchId, makeTestUuid(DUEL_TEST_MATCH_ID));
    nodeNetworkPacketSetUuid(packet.senderPlayerId, makeTestUuid(DUEL_TEST_PEER_ID));
    nodeNetworkPacketSetUuid(packet.senderTeamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
    packet.occurredAtUnixMs = 0;
    return packet;
}

inline int feedNodeNetworkPacket(NodeNetworkWirelessManager& manager, const uint8_t* senderMac,
                                 const NodeNetworkPacket& packet) {
    return manager.processPacket(senderMac, reinterpret_cast<const uint8_t*>(&packet),
                                 sizeof(packet));
}

struct ReceivedNodeNetworkPacket {
    int callCount = 0;
    std::array<uint8_t, 6> senderMac = {};
    NodeNetworkPacket packet;
};

struct NodeNetworkDeliveryReport {
    int callCount = 0;
    bool delivered = false;
    NodeNetworkPacket packet;
};

inline void nodeNetworkPacketLayoutIsPinned() {
    EXPECT_EQ(sizeof(NodeNetworkPacket), NODE_NETWORK_PACKET_WIRE_SIZE);
}

inline void nodeNetworkWirelessDeliversDecodedPacket() {
    NodeNetworkWirelessManager manager;
    ReceivedNodeNetworkPacket received;
    manager.setPacketReceivedCallback([&received](const uint8_t* senderMac, const NodeNetworkPacket& packet) {
        received.callCount++;
        memcpy(received.senderMac.data(), senderMac, 6);
        received.packet = packet;
    });
    NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::SEQUENCE, true);
    sent.stepIndex = 3;
    sent.inputSuccess = 1;
    sent.pressCount = 2;
    const uint8_t prompts[NODE_NETWORK_PACKET_STEPS] = {0x01, 0x50, 0xE2, 0x00, 0x21};
    memcpy(sent.stepPrompts, prompts, sizeof(prompts));
    sent.initiatorScore = 2;
    sent.responderScore = 3;
    sent.syncOriginMs = 0x01020304;
    sent.syncReceiveMs = 0x05060708;
    sent.syncTransmitMs = 0x090A0B0C;
    sent.stepZeroAtMs = 0x0D0E0F10;
    sent.occurredAtUnixMs = DUEL_TEST_UNIX_MS;

    const int result = feedNodeNetworkPacket(manager, DUEL_TEST_PEER_MAC, sent);

    EXPECT_EQ(result, 1);
    ASSERT_EQ(received.callCount, 1);
    EXPECT_EQ(memcmp(received.senderMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
    EXPECT_EQ(memcmp(&received.packet, &sent, sizeof(sent)), 0);
}

inline void nodeNetworkWirelessDropsWrongLength() {
    NodeNetworkWirelessManager manager;
    ReceivedNodeNetworkPacket received;
    manager.setPacketReceivedCallback([&received](const uint8_t*, const NodeNetworkPacket&) {
        received.callCount++;
    });
    const NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);

    EXPECT_EQ(manager.processPacket(DUEL_TEST_PEER_MAC, reinterpret_cast<const uint8_t*>(&sent),
                                    sizeof(sent) - 1),
              -1);
    EXPECT_EQ(manager.processPacket(DUEL_TEST_PEER_MAC, reinterpret_cast<const uint8_t*>(&sent),
                                    sizeof(sent) + 1),
              -1);
    EXPECT_EQ(received.callCount, 0);
}

inline void nodeNetworkWirelessDropsUnknownCommand() {
    NodeNetworkWirelessManager manager;
    ReceivedNodeNetworkPacket received;
    manager.setPacketReceivedCallback([&received](const uint8_t*, const NodeNetworkPacket&) {
        received.callCount++;
    });
    NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    sent.command = 0xEE;

    EXPECT_EQ(feedNodeNetworkPacket(manager, DUEL_TEST_PEER_MAC, sent), -1);
    sent.command = 0;
    EXPECT_EQ(feedNodeNetworkPacket(manager, DUEL_TEST_PEER_MAC, sent), -1);
    EXPECT_EQ(received.callCount, 0);
}

inline void nodeNetworkWirelessAcceptsInputPress() {
    NodeNetworkWirelessManager manager;
    const NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::INPUT_PRESS, true);

    EXPECT_EQ(feedNodeNetworkPacket(manager, DUEL_TEST_PEER_MAC, sent), 1);
}

inline void nodeNetworkWirelessSurvivesMissingCallback() {
    NodeNetworkWirelessManager manager;
    const NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);

    EXPECT_EQ(feedNodeNetworkPacket(manager, DUEL_TEST_PEER_MAC, sent), 1);
}

inline void nodeNetworkWirelessClearCallbacksStopsDelivery() {
    NodeNetworkWirelessManager manager;
    ReceivedNodeNetworkPacket received;
    manager.setPacketReceivedCallback([&received](const uint8_t*, const NodeNetworkPacket&) {
        received.callCount++;
    });
    manager.clearCallbacks();
    const NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);

    feedNodeNetworkPacket(manager, DUEL_TEST_PEER_MAC, sent);

    EXPECT_EQ(received.callCount, 0);
}

// ============================================
// Reliable delivery
// ============================================

/** A reliable sender with a test clock and captured sends and delivery reports. */
class NodeNetworkReliableSuite : public testing::Test {
public:
    /** @copydoc testing::Test::SetUp */
    void SetUp() override {
        clock.setTime(1000);
        wireless.setClock(&clock);
        wireless.setDeliveryCallback(
        [this](const uint8_t*, const NodeNetworkPacket& packet, bool delivered) {
            report.callCount++;
            report.delivered = delivered;
            report.packet = packet;
        });
    }

    /** The ACK a peer would send for @p packet. */
    static NodeNetworkPacket ackFor(const NodeNetworkPacket& packet) {
        NodeNetworkPacket ack;
        ack.command = static_cast<uint8_t>(NodeNetworkCommand::ACK);
        ack.sequenceNumber = packet.sequenceNumber;
        memcpy(ack.matchId, packet.matchId, sizeof(ack.matchId));
        return ack;
    }

    FakePlatformClock clock;
    FakeNodeNetworkWirelessManager wireless;
    NodeNetworkDeliveryReport report;
};

inline void nodeNetworkReliableSendMarksPacket(NodeNetworkReliableSuite* suite) {
    const NodeNetworkPacket packet = makeNodeNetworkPacket(NodeNetworkCommand::SEQUENCE, true);

    EXPECT_EQ(suite->wireless.sendReliable(DUEL_TEST_PEER_MAC, packet), 0);
    EXPECT_EQ(suite->wireless.sendReliable(DUEL_TEST_PEER_MAC, packet), 0);

    ASSERT_EQ(suite->wireless.sent.size(), 2u);
    EXPECT_EQ(suite->wireless.sent[0].packet.reliable, 1);
    EXPECT_NE(suite->wireless.sent[0].packet.sequenceNumber,
              suite->wireless.sent[1].packet.sequenceNumber);
    EXPECT_EQ(suite->wireless.getPendingReliableCount(), 2u);
}

inline void nodeNetworkReliableResendsUntilAcked(NodeNetworkReliableSuite* suite) {
    suite->wireless.sendReliable(DUEL_TEST_PEER_MAC,
                                 makeNodeNetworkPacket(NodeNetworkCommand::OUTCOME, true));
    const NodeNetworkPacket first = suite->wireless.sent[0].packet;

    suite->clock.advance(NODE_RELIABLE_RETRY_MS - 1);
    suite->wireless.update();
    EXPECT_EQ(suite->wireless.sent.size(), 1u);

    suite->clock.advance(1);
    suite->wireless.update();
    ASSERT_EQ(suite->wireless.sent.size(), 2u);
    EXPECT_EQ(memcmp(&suite->wireless.sent[1].packet, &first, sizeof(first)), 0);

    feedNodeNetworkPacket(suite->wireless, DUEL_TEST_PEER_MAC,
                          NodeNetworkReliableSuite::ackFor(first));
    EXPECT_EQ(suite->report.callCount, 1);
    EXPECT_TRUE(suite->report.delivered);
    EXPECT_EQ(suite->report.packet.command, static_cast<uint8_t>(NodeNetworkCommand::OUTCOME));
    EXPECT_EQ(suite->wireless.getPendingReliableCount(), 0u);

    suite->clock.advance(NODE_RELIABLE_RETRY_MS * 5);
    suite->wireless.update();
    EXPECT_EQ(suite->wireless.sent.size(), 2u);
}

inline void nodeNetworkReliableReportsUndelivered(NodeNetworkReliableSuite* suite) {
    suite->wireless.sendReliable(DUEL_TEST_PEER_MAC,
                                 makeNodeNetworkPacket(NodeNetworkCommand::ABORT, true));

    for (int tick = 0; tick < NODE_RELIABLE_MAX_ATTEMPTS + 2; tick++) {
        suite->clock.advance(NODE_RELIABLE_RETRY_MS);
        suite->wireless.update();
    }

    EXPECT_EQ(suite->wireless.sent.size(), static_cast<size_t>(NODE_RELIABLE_MAX_ATTEMPTS));
    EXPECT_EQ(suite->report.callCount, 1);
    EXPECT_FALSE(suite->report.delivered);
    EXPECT_EQ(suite->wireless.getPendingReliableCount(), 0u);
}

inline void nodeNetworkReliableIgnoresAckFromOtherMac(NodeNetworkReliableSuite* suite) {
    suite->wireless.sendReliable(DUEL_TEST_PEER_MAC,
                                 makeNodeNetworkPacket(NodeNetworkCommand::OUTCOME, true));

    feedNodeNetworkPacket(suite->wireless, DUEL_TEST_OTHER_MAC,
                          NodeNetworkReliableSuite::ackFor(suite->wireless.sent[0].packet));

    EXPECT_EQ(suite->report.callCount, 0);
    EXPECT_EQ(suite->wireless.getPendingReliableCount(), 1u);
}

inline void nodeNetworkReliableRefusesWhenTableFull(NodeNetworkReliableSuite* suite) {
    const NodeNetworkPacket packet = makeNodeNetworkPacket(NodeNetworkCommand::INPUT_RESULT, true);
    for (size_t slot = 0; slot < NODE_RELIABLE_PENDING_CAPACITY; slot++) {
        ASSERT_EQ(suite->wireless.sendReliable(DUEL_TEST_PEER_MAC, packet), 0);
    }

    EXPECT_EQ(suite->wireless.sendReliable(DUEL_TEST_PEER_MAC, packet), -1);
    EXPECT_EQ(suite->wireless.sent.size(), NODE_RELIABLE_PENDING_CAPACITY);
}

inline void nodeNetworkReliableReceiverAcksAndDropsRepeats(NodeNetworkReliableSuite* suite) {
    ReceivedNodeNetworkPacket received;
    suite->wireless.setPacketReceivedCallback(
    [&received](const uint8_t*, const NodeNetworkPacket&) { received.callCount++; });
    NodeNetworkPacket packet = makeNodeNetworkPacket(NodeNetworkCommand::INPUT_RESULT, true);
    packet.reliable = 1;
    packet.sequenceNumber = 42;

    feedNodeNetworkPacket(suite->wireless, DUEL_TEST_PEER_MAC, packet);
    feedNodeNetworkPacket(suite->wireless, DUEL_TEST_PEER_MAC, packet);

    EXPECT_EQ(received.callCount, 1);
    ASSERT_EQ(suite->wireless.sent.size(), 2u);
    for (const FakeNodeNetworkWirelessManager::SentPacket& ack : suite->wireless.sent) {
        EXPECT_EQ(ack.packet.command, static_cast<uint8_t>(NodeNetworkCommand::ACK));
        EXPECT_EQ(ack.packet.sequenceNumber, 42);
        EXPECT_EQ(memcmp(ack.destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
        EXPECT_EQ(nodeNetworkPacketGetUuid(ack.packet.matchId), makeTestUuid(DUEL_TEST_MATCH_ID));
    }

    packet.sequenceNumber = 43;
    feedNodeNetworkPacket(suite->wireless, DUEL_TEST_PEER_MAC, packet);
    EXPECT_EQ(received.callCount, 2);
}

inline void nodeNetworkReliableAckIsNotDelivered(NodeNetworkReliableSuite* suite) {
    ReceivedNodeNetworkPacket received;
    suite->wireless.setPacketReceivedCallback(
    [&received](const uint8_t*, const NodeNetworkPacket&) { received.callCount++; });
    NodeNetworkPacket ack;
    ack.command = static_cast<uint8_t>(NodeNetworkCommand::ACK);

    EXPECT_EQ(feedNodeNetworkPacket(suite->wireless, DUEL_TEST_PEER_MAC, ack), 1);

    EXPECT_EQ(received.callCount, 0);
    EXPECT_TRUE(suite->wireless.sent.empty());
}

inline void nodeNetworkUnreliablePacketIsNotAcked(NodeNetworkReliableSuite* suite) {
    feedNodeNetworkPacket(suite->wireless, DUEL_TEST_PEER_MAC,
                          makeNodeNetworkPacket(NodeNetworkCommand::SYNC, true));

    EXPECT_TRUE(suite->wireless.sent.empty());
}
