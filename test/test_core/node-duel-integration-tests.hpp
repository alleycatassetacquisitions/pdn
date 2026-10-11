#pragma once

#include <gtest/gtest.h>
#include <functional>
#include "game/node-match-manager.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "utility-tests.hpp"

class NodeDuelIntegrationTestSuite : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        initiatorPlayer = new Player();
        char initiatorId[] = "in01";
        initiatorPlayer->setUserID(initiatorId);
        initiatorPlayer->setIsHunter(true);

        responderPlayer = new Player();
        char responderId[] = "rs01";
        responderPlayer->setUserID(responderId);
        responderPlayer->setIsHunter(true);

        initiatorWireless = new FakeQuickdrawWirelessManager();
        responderWireless = new FakeQuickdrawWirelessManager();
        initiatorWireless->initialize(initiatorPlayer, nullptr, 0);
        responderWireless->initialize(responderPlayer, nullptr, 0);

        initiatorMatchManager = new NodeMatchManager();
        responderMatchManager = new NodeMatchManager();
        initiatorMatchManager->initialize(initiatorPlayer, &initiatorStorage, initiatorWireless);
        responderMatchManager->initialize(responderPlayer, &responderStorage, responderWireless);

        initiatorRdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, responderMac);
        responderRdc.setPeerMac(SerialIdentifier::INPUT_JACK, initiatorMac);
        initiatorMatchManager->setRemoteDeviceCoordinator(&initiatorRdc);
        responderMatchManager->setRemoteDeviceCoordinator(&responderRdc);

        initiatorWireless->setPacketReceivedCallback(
            std::bind(&NodeMatchManager::listenForMatchEvents, initiatorMatchManager, std::placeholders::_1));
        responderWireless->setPacketReceivedCallback(
            std::bind(&NodeMatchManager::listenForMatchEvents, responderMatchManager, std::placeholders::_1));

        using ::testing::_;
        ON_CALL(initiatorStorage, end()).WillByDefault([]() {});
        ON_CALL(responderStorage, end()).WillByDefault([]() {});
        ON_CALL(initiatorStorage, readUChar(_, _, _)).WillByDefault(testing::Return(0));
        ON_CALL(responderStorage, readUChar(_, _, _)).WillByDefault(testing::Return(0));
        ON_CALL(initiatorStorage, write(_, _, _)).WillByDefault([](const std::string&, const std::string&, const std::string& value) {
            return value.size();
        });
        ON_CALL(responderStorage, write(_, _, _)).WillByDefault([](const std::string&, const std::string&, const std::string& value) {
            return value.size();
        });
        ON_CALL(initiatorStorage, writeUChar(_, _, _)).WillByDefault(testing::Return(1));
        ON_CALL(responderStorage, writeUChar(_, _, _)).WillByDefault(testing::Return(1));
    }

    void TearDown() override {
        initiatorMatchManager->clearCurrentMatch();
        responderMatchManager->clearCurrentMatch();
        delete initiatorMatchManager;
        delete responderMatchManager;
        delete initiatorWireless;
        delete responderWireless;
        delete initiatorPlayer;
        delete responderPlayer;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    void performHandshake() {
        initiatorMatchManager->initializeMatch(responderMac);
        initiatorWireless->deliverLastTo(responderWireless, initiatorMac);
        responderWireless->deliverLastTo(initiatorWireless, responderMac);
    }

    static uint8_t initiatorMac[6];
    static uint8_t responderMac[6];

    FakePlatformClock* fakeClock = nullptr;
    Player* initiatorPlayer = nullptr;
    Player* responderPlayer = nullptr;
    NodeMatchManager* initiatorMatchManager = nullptr;
    NodeMatchManager* responderMatchManager = nullptr;
    FakeQuickdrawWirelessManager* initiatorWireless = nullptr;
    FakeQuickdrawWirelessManager* responderWireless = nullptr;
    testing::NiceMock<MockStorage> initiatorStorage;
    testing::NiceMock<MockStorage> responderStorage;
    FakeRemoteDeviceCoordinator initiatorRdc;
    FakeRemoteDeviceCoordinator responderRdc;
};

uint8_t NodeDuelIntegrationTestSuite::initiatorMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
uint8_t NodeDuelIntegrationTestSuite::responderMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

inline void nodeDuelIntegrationSyncSuccessBothSides(NodeDuelIntegrationTestSuite* suite) {
    suite->performHandshake();

    suite->initiatorMatchManager->setDuelLocalStartTime(10000);
    suite->responderMatchManager->setDuelLocalStartTime(10000);

    suite->initiatorMatchManager->setHunterDrawTime(200);
    suite->initiatorMatchManager->setReceivedButtonPush();
    suite->responderMatchManager->setBountyDrawTime(300);
    suite->responderMatchManager->setReceivedButtonPush();

    suite->initiatorMatchManager->setBountyDrawTime(300);
    suite->initiatorMatchManager->setReceivedDrawResult();
    suite->responderMatchManager->setHunterDrawTime(200);
    suite->responderMatchManager->setReceivedDrawResult();

    EXPECT_TRUE(suite->initiatorMatchManager->didSync());
    EXPECT_TRUE(suite->responderMatchManager->didSync());
    EXPECT_TRUE(suite->initiatorMatchManager->finalizeMatch());
    EXPECT_TRUE(suite->responderMatchManager->finalizeMatch());
}

inline void nodeDuelIntegrationSyncFailNoFinalize(NodeDuelIntegrationTestSuite* suite) {
    suite->performHandshake();

    suite->initiatorMatchManager->setHunterDrawTime(100);
    suite->initiatorMatchManager->setReceivedButtonPush();
    suite->responderMatchManager->setBountyDrawTime(500);
    suite->responderMatchManager->setReceivedButtonPush();

    suite->initiatorMatchManager->setBountyDrawTime(500);
    suite->initiatorMatchManager->setReceivedDrawResult();
    suite->responderMatchManager->setHunterDrawTime(100);
    suite->responderMatchManager->setReceivedDrawResult();

    EXPECT_FALSE(suite->initiatorMatchManager->didSync());
    suite->initiatorMatchManager->recordLinkOutcome();
    EXPECT_FALSE(suite->initiatorMatchManager->finalizeMatch());
    EXPECT_EQ(suite->initiatorMatchManager->getStoredMatchCount(), 0u);
}
