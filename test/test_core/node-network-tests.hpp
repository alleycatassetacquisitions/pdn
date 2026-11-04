#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "game/node-network/node-network.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "utility-tests.hpp"

using ::testing::_;
using ::testing::Return;

class NodeNetworkTestSuite : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(0);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, renderGlyph(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawButton(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());

        symbolWireless = new SymbolWirelessManager();
        symbolWireless->initialize(device.getWirelessManager(), device.getRemoteDeviceCoordinator());
        nodeNetwork = new NodeNetwork(&device, &wireless, symbolWireless);
    }

    void TearDown() override {
        delete nodeNetwork;
        nodeNetwork = nullptr;
        delete symbolWireless;
        symbolWireless = nullptr;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice device;
    FakeNodeNetworkWirelessManager wireless;
    SymbolWirelessManager* symbolWireless = nullptr;
    NodeNetwork* nodeNetwork = nullptr;
};

inline void nodeNetworkOwnsProviderAndMatchManager(NodeNetworkTestSuite* suite) {
    EXPECT_NE(suite->nodeNetwork->getPlayerProvider(), nullptr);
    EXPECT_NE(suite->nodeNetwork->getMatchManager(), nullptr);
    EXPECT_EQ(suite->nodeNetwork->getStateId(), NODE_NETWORK_APP_ID);
}

inline void nodeNetworkStartsInPlayerFetch(NodeNetworkTestSuite* suite) {
    suite->nodeNetwork->initialize(&suite->device);

    ASSERT_NE(suite->nodeNetwork->getCurrentState(), nullptr);
    EXPECT_EQ(suite->nodeNetwork->getCurrentState()->getStateId(), PLAYER_FETCH_APP_ID);
}

inline void nodeNetworkStaysInPlayerFetchBeforeWelcomeFinishes(NodeNetworkTestSuite* suite) {
    suite->nodeNetwork->initialize(&suite->device);

    suite->nodeNetwork->onStateLoop(&suite->device);

    EXPECT_EQ(suite->nodeNetwork->getCurrentState()->getStateId(), PLAYER_FETCH_APP_ID);
}

inline void nodeNetworkEntersNodeIdleWhenWelcomeFinishes(NodeNetworkTestSuite* suite) {
    suite->nodeNetwork->initialize(&suite->device);
    PlayerFetchApp* playerFetch = static_cast<PlayerFetchApp*>(suite->nodeNetwork->getCurrentState());
    ASSERT_TRUE(playerFetch->skipToState(&suite->device, 4));
    ASSERT_EQ(playerFetch->getCurrentState()->getStateId(), PlayerFetchStateId::WELCOME);

    suite->fakeClock->setTime(WELCOME_MESSAGE_TIMEOUT_MS + 1);
    suite->nodeNetwork->onStateLoop(&suite->device);
    suite->nodeNetwork->onStateLoop(&suite->device);

    EXPECT_EQ(suite->nodeNetwork->getCurrentState()->getStateId(), NODE_IDLE);
}

/** NodeIdle driven against a fake cable: port statuses and peer types are scripted per test. */
class NodeIdleLinkTestSuite : public testing::Test {
public:
    void SetUp() override {
        ON_CALL(*pdn.mockDisplay, invalidateScreen()).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawImage(_)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawText(_, _, _)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, setGlyphMode(_)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, render()).WillByDefault(Return());

        node = new NodeNetworkDevice(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM,
                                     NODE_NETWORK_TEST_MAC_A, DUEL_TEST_PEER_MAC,
                                     NODE_NETWORK_TEST_INITIATOR_MS);
        SimpleTimer::setPlatformClock(&node->clock);
        node->clock.setTime(0);
        stats = new NodeNetworkStats(&statsStorage);
        idle = new NodeIdle(node->provider.getPlayer(), node->manager, stats, &node->rdc);
    }

    void TearDown() override {
        delete idle;
        delete stats;
        delete node;
        SimpleTimer::setPlatformClock(nullptr);
    }

    void linkJack(SerialIdentifier jack, PortStatus status, DeviceType type = DeviceType::PDN) {
        node->rdc.setPortStatus(jack, status);
        node->rdc.setPeerDeviceType(jack, type);
    }

    size_t offersSent() const {
        size_t count = 0;
        for (const auto& sent : node->wireless.sent) {
            if (sent.packet.command == static_cast<uint8_t>(NodeNetworkCommand::OFFER)) {
                count++;
            }
        }
        return count;
    }

    MockDevice pdn;
    InMemoryStorage statsStorage;
    NodeNetworkDevice* node = nullptr;
    NodeNetworkStats* stats = nullptr;
    NodeIdle* idle = nullptr;
};

inline void nodeIdleOffersWhenOutputJackLinked(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->node->manager->getLocalLinkSide(), DuelLinkSide::INITIATOR);
    EXPECT_EQ(suite->offersSent(), 1u);
}

inline void nodeIdleWaitsAsResponderWhenInputJackLinked(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::INPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->node->manager->getLocalLinkSide(), DuelLinkSide::RESPONDER);
    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleIgnoresJackStillConnecting(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTING);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleIgnoresDaisyChainedJack(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::DAISY_CHAINED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleIgnoresPdnLinkedOnBothJacks(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    suite->linkJack(SerialIdentifier::INPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleReoffersWhenOfferGoesUnanswered(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);
    suite->node->clock.advance(500);
    suite->idle->onStateLoop(&suite->pdn);
    EXPECT_EQ(suite->offersSent(), 1u);

    suite->node->clock.advance(600);
    suite->idle->onStateLoop(&suite->pdn);
    EXPECT_EQ(suite->offersSent(), 2u);
    EXPECT_TRUE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleStopsOfferingWhenLinkDrops(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    suite->idle->onStateLoop(&suite->pdn);
    ASSERT_TRUE(suite->node->manager->getSession().isActive());

    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleRoutesFdnToSymbolFlow(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::INPUT_JACK, PortStatus::CONNECTED, DeviceType::FDN);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->idle->transitionToSymbol());
    EXPECT_EQ(suite->offersSent(), 0u);
}

inline void nodeNetworkIgnoresOfferBeforeLinkSideIsAssigned() {
    NodeNetworkDevice responder(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM,
                                NODE_NETWORK_TEST_MAC_A, DUEL_TEST_PEER_MAC,
                                NODE_NETWORK_TEST_RESPONDER_MS);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    responder.manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_TRUE(responder.wireless.sent.empty());
    EXPECT_FALSE(responder.manager->getSession().isActive());
}

inline void nodeNetworkRegistersOnlyItsOwnPacketHandler(NodeNetworkTestSuite* suite) {
    delete suite->nodeNetwork;
    suite->nodeNetwork = nullptr;
    SymbolWirelessManager symbolWireless;
    symbolWireless.initialize(suite->device.getWirelessManager(),
                              suite->device.getRemoteDeviceCoordinator());
    EXPECT_CALL(*suite->device.mockPeerComms, setPacketHandler(PktType::kNodeNetworkCommand, _, _))
        .Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms, setPacketHandler(PktType::kSymbolMatchCommand, _, _))
        .Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms, clearPacketHandler(PktType::kNodeNetworkCommand))
        .Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms, clearPacketHandler(PktType::kSymbolMatchCommand))
        .Times(1);

    NodeNetwork* nodeNetwork = new NodeNetwork(&suite->device, &suite->wireless, &symbolWireless);
    delete nodeNetwork;
}