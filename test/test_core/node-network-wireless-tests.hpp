#pragma once

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "duel-session-tests.hpp"
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

    std::vector<SentPacket> sent;
};

inline NodeNetworkPacket makeNodeNetworkPacket(NodeNetworkCommand command, bool senderIsInitiator) {
    NodeNetworkPacket packet;
    packet.protocolVersion = NODE_NETWORK_PROTOCOL_VERSION;
    packet.command = command;
    packet.senderIsInitiator = senderIsInitiator ? 1 : 0;
    packet.matchId = makeTestUuid(DUEL_TEST_MATCH_ID);
    packet.senderPlayerId = makeTestUuid(DUEL_TEST_PEER_ID);
    packet.senderTeamId = makeTestUuid(DUEL_TEST_PEER_TEAM);
    packet.drawTimeMs = 0;
    packet.occurredAtUnixMs = 0;
    return packet;
}

struct ReceivedNodeNetworkPacket {
    int callCount = 0;
    std::array<uint8_t, 6> senderMac = {};
    NodeNetworkPacket packet;
};

inline void nodeNetworkPacketLayoutIsPinned() {
    EXPECT_EQ(sizeof(NodeNetworkPacket), 67u);
}

inline void nodeNetworkWirelessDeliversDecodedPacket() {
    NodeNetworkWirelessManager manager;
    ReceivedNodeNetworkPacket received;
    manager.setPacketReceivedCallback([&received](const uint8_t* senderMac, const NodeNetworkPacket& packet) {
        received.callCount++;
        memcpy(received.senderMac.data(), senderMac, 6);
        received.packet = packet;
    });
    NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::DRAW, false);
    sent.drawTimeMs = 215;
    sent.occurredAtUnixMs = DUEL_TEST_UNIX_MS;

    const int result = manager.processPacket(DUEL_TEST_PEER_MAC,
                                             reinterpret_cast<const uint8_t*>(&sent), sizeof(sent));

    EXPECT_EQ(result, 1);
    ASSERT_EQ(received.callCount, 1);
    EXPECT_EQ(memcmp(received.senderMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
    EXPECT_EQ(received.packet.command, NodeNetworkCommand::DRAW);
    EXPECT_EQ(received.packet.senderIsInitiator, 0);
    EXPECT_EQ(received.packet.matchId, sent.matchId);
    EXPECT_EQ(received.packet.senderPlayerId, sent.senderPlayerId);
    EXPECT_EQ(received.packet.senderTeamId, sent.senderTeamId);
    EXPECT_EQ(received.packet.drawTimeMs, 215u);
    EXPECT_EQ(received.packet.occurredAtUnixMs, DUEL_TEST_UNIX_MS);
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

inline void nodeNetworkWirelessDropsWrongVersion() {
    NodeNetworkWirelessManager manager;
    ReceivedNodeNetworkPacket received;
    manager.setPacketReceivedCallback([&received](const uint8_t*, const NodeNetworkPacket&) {
        received.callCount++;
    });
    NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    sent.protocolVersion = NODE_NETWORK_PROTOCOL_VERSION + 1;

    EXPECT_EQ(manager.processPacket(DUEL_TEST_PEER_MAC, reinterpret_cast<const uint8_t*>(&sent),
                                    sizeof(sent)),
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
    uint8_t* commandByte = reinterpret_cast<uint8_t*>(&sent) + offsetof(NodeNetworkPacket, command);
    *commandByte = 0xEE;

    EXPECT_EQ(manager.processPacket(DUEL_TEST_PEER_MAC, reinterpret_cast<const uint8_t*>(&sent),
                                    sizeof(sent)),
              -1);
    EXPECT_EQ(received.callCount, 0);
}

inline void nodeNetworkWirelessSurvivesMissingCallback() {
    NodeNetworkWirelessManager manager;
    const NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);

    EXPECT_EQ(manager.processPacket(DUEL_TEST_PEER_MAC, reinterpret_cast<const uint8_t*>(&sent),
                                    sizeof(sent)),
              1);
}

inline void nodeNetworkWirelessClearCallbacksStopsDelivery() {
    NodeNetworkWirelessManager manager;
    ReceivedNodeNetworkPacket received;
    manager.setPacketReceivedCallback([&received](const uint8_t*, const NodeNetworkPacket&) {
        received.callCount++;
    });
    manager.clearCallbacks();
    const NodeNetworkPacket sent = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);

    manager.processPacket(DUEL_TEST_PEER_MAC, reinterpret_cast<const uint8_t*>(&sent), sizeof(sent));

    EXPECT_EQ(received.callCount, 0);
}
