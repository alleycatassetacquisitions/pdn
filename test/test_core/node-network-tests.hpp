#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "game/node-network/node-network.hpp"
#include "node-network-wireless-tests.hpp"
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
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, renderGlyph(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawButton(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());

        nodeNetwork = new NodeNetwork(&device, &wireless);
    }

    void TearDown() override {
        delete nodeNetwork;
        nodeNetwork = nullptr;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice device;
    FakeNodeNetworkWirelessManager wireless;
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

inline void nodeNetworkRegistersOnlyItsOwnPacketHandler(NodeNetworkTestSuite* suite) {
    delete suite->nodeNetwork;
    suite->nodeNetwork = nullptr;
    EXPECT_CALL(*suite->device.mockPeerComms, setPacketHandler(_, _, _)).Times(0);
    EXPECT_CALL(*suite->device.mockPeerComms, setPacketHandler(PktType::kNodeNetworkCommand, _, _))
        .Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms, clearPacketHandler(PktType::kNodeNetworkCommand))
        .Times(1);

    NodeNetwork* nodeNetwork = new NodeNetwork(&suite->device, &suite->wireless);
    delete nodeNetwork;
}