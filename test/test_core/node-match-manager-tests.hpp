#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <functional>
#include "game/node-match-manager.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "utility-tests.hpp"

class NodeMatchManagerTestSuite : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        matchManager = new NodeMatchManager();
        player = new Player();
        char playerId[] = "plr1";
        player->setUserID(playerId);

        matchManager->initialize(player, &mockStorage, &fakeWirelessManager);
        using ::testing::_;
        ON_CALL(mockStorage, end()).WillByDefault([]() {});
        ON_CALL(mockStorage, readUChar(_, _, _)).WillByDefault(testing::Return(0));
        fakeWirelessManager.setPacketReceivedCallback(
            std::bind(&NodeMatchManager::listenForMatchEvents, matchManager, std::placeholders::_1));

        uint8_t defaultPeerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, defaultPeerMac);
        matchManager->setRemoteDeviceCoordinator(&fakeRdc);
        matchManager->clearCurrentMatch();
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    void setupMatchAsInitiator() {
        uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        matchManager->initializeMatch(peerMac);
    }

    void setupMatchAsResponder(const char* matchId = "test-match-id",
                               const char* initiatorId = "init") {
        static const uint8_t initiatorMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        QuickdrawCommand cmd(initiatorMac, QDCommand::SEND_MATCH_ID, matchId, initiatorId, 0, true);
        matchManager->listenForMatchEvents(cmd);
    }

    NodeMatchManager* matchManager = nullptr;
    Player* player = nullptr;
    MockStorage mockStorage;
    FakeQuickdrawWirelessManager fakeWirelessManager;
    FakeRemoteDeviceCoordinator fakeRdc;
    FakePlatformClock* fakeClock = nullptr;
};

inline void nodeMatchManagerInitializeAsInitiatorCreatesMatch(NodeMatchManagerTestSuite* suite) {
    suite->player->setIsHunter(true);
    suite->setupMatchAsInitiator();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getDuelLinkSide(), DuelLinkSide::Initiator);
    EXPECT_STREQ(suite->matchManager->getCurrentMatch()->getHunterId(), suite->player->getUserID().c_str());
    EXPECT_TRUE(suite->matchManager->localSideIsInitiator());
}

inline void nodeMatchManagerInitiatorSendsWireIsInitiator(NodeMatchManagerTestSuite* suite) {
    suite->player->setIsHunter(false);
    suite->setupMatchAsInitiator();

    ASSERT_FALSE(suite->fakeWirelessManager.sentCommands.empty());
    EXPECT_TRUE(suite->fakeWirelessManager.sentCommands.back().isHunter);
    EXPECT_EQ(suite->fakeWirelessManager.sentCommands.back().command, QDCommand::SEND_MATCH_ID);
}

inline void nodeMatchManagerResponderReceivesHandshake(NodeMatchManagerTestSuite* suite) {
    suite->player->setIsHunter(true);
    suite->setupMatchAsResponder("recv-match", "peer");

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getDuelLinkSide(), DuelLinkSide::Responder);
    EXPECT_STREQ(suite->matchManager->getCurrentMatch()->getHunterId(), "peer");
    EXPECT_STREQ(suite->matchManager->getCurrentMatch()->getBountyId(), suite->player->getUserID().c_str());
    EXPECT_TRUE(suite->matchManager->isMatchReady());
}

class NodeDuelHandshakeTest : public testing::Test {
protected:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
        fakeClock = nullptr;
    }

    FakePlatformClock* fakeClock = nullptr;
};

inline void nodeMatchManagerSameRegistrationRoleHandshake(NodeDuelHandshakeTest* /*suite*/) {
    static uint8_t initiatorMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    static uint8_t responderMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

    Player* initiatorPlayer = new Player();
    char initiatorId[] = "in01";
    initiatorPlayer->setUserID(initiatorId);
    initiatorPlayer->setIsHunter(true);

    Player* responderPlayer = new Player();
    char responderId[] = "rs01";
    responderPlayer->setUserID(responderId);
    responderPlayer->setIsHunter(true);

    FakeRemoteDeviceCoordinator initiatorRdc;
    FakeRemoteDeviceCoordinator responderRdc;
    initiatorRdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, responderMac);
    responderRdc.setPeerMac(SerialIdentifier::INPUT_JACK, initiatorMac);

    FakeQuickdrawWirelessManager* initiatorWireless = new FakeQuickdrawWirelessManager();
    FakeQuickdrawWirelessManager* responderWireless = new FakeQuickdrawWirelessManager();

    NodeMatchManager* initiatorMm = new NodeMatchManager();
    NodeMatchManager* responderMm = new NodeMatchManager();
    MockStorage storageA;
    MockStorage storageB;
    using ::testing::_;
    ON_CALL(storageA, end()).WillByDefault([]() {});
    ON_CALL(storageB, end()).WillByDefault([]() {});
    ON_CALL(storageA, readUChar(_, _, _)).WillByDefault(testing::Return(0));
    ON_CALL(storageB, readUChar(_, _, _)).WillByDefault(testing::Return(0));

    initiatorWireless->initialize(initiatorPlayer, nullptr, 0);
    responderWireless->initialize(responderPlayer, nullptr, 0);
    initiatorMm->initialize(initiatorPlayer, &storageA, initiatorWireless);
    responderMm->initialize(responderPlayer, &storageB, responderWireless);
    initiatorMm->setRemoteDeviceCoordinator(&initiatorRdc);
    responderMm->setRemoteDeviceCoordinator(&responderRdc);

    initiatorMm->initializeMatch(responderMac);
    ASSERT_TRUE(initiatorMm->getCurrentMatch().has_value());
    const char* matchId = initiatorMm->getCurrentMatch()->getMatchId();

    QuickdrawCommand sendMatch(initiatorMac, QDCommand::SEND_MATCH_ID, matchId, initiatorId, 0, true);
    responderMm->listenForMatchEvents(sendMatch);

    QuickdrawCommand ack(responderMac, QDCommand::MATCH_ID_ACK, matchId, responderId, 0, false);
    initiatorMm->listenForMatchEvents(ack);

    EXPECT_TRUE(initiatorMm->isMatchReady());
    EXPECT_TRUE(responderMm->isMatchReady());

    delete initiatorMm;
    delete responderMm;
    delete initiatorWireless;
    delete responderWireless;
    delete initiatorPlayer;
    delete responderPlayer;
}

inline void nodeMatchManagerSyncSuccessWhenDeltaWithinWindow(NodeMatchManager* mm) {
    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(peerMac);
    mm->setHunterDrawTime(200);
    mm->setBountyDrawTime(350);
    mm->setReceivedButtonPush();
    mm->setReceivedDrawResult();
    EXPECT_TRUE(mm->didSync());
    EXPECT_EQ(mm->syncDeltaMs(), 150u);
}

inline void nodeMatchManagerSyncFailsWhenDeltaTooLarge(NodeMatchManager* mm) {
    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(peerMac);
    mm->setHunterDrawTime(200);
    mm->setBountyDrawTime(500);
    mm->setReceivedButtonPush();
    mm->setReceivedDrawResult();
    EXPECT_FALSE(mm->didSync());
    EXPECT_TRUE(mm->linkFailed());
}

inline void nodeMatchManagerFailedAttemptsIncrementOnFail(NodeMatchManager* mm) {
    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(peerMac);
    mm->setHunterDrawTime(100);
    mm->setBountyDrawTime(500);
    mm->setReceivedButtonPush();
    mm->setReceivedDrawResult();
    mm->recordLinkOutcome();
    EXPECT_EQ(mm->getFailedAttempts(), 1u);
    EXPECT_FALSE(mm->finalizeMatch());
}

inline void nodeMatchManagerOpponentNeverPressedIsNotSync(NodeMatchManager* mm) {
    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(peerMac);
    mm->setHunterDrawTime(200);
    mm->setReceivedButtonPush();
    mm->setOpponentNeverPressed();
    EXPECT_TRUE(mm->matchResultsAreIn());
    EXPECT_FALSE(mm->didSync());
}

inline void nodeMatchManagerInitiatorNotReadyBeforeAck(NodeMatchManagerTestSuite* suite) {
    suite->setupMatchAsInitiator();
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(suite->matchManager->isMatchReady());
}

inline void nodeMatchManagerInitiatorReadyAfterAck(NodeMatchManagerTestSuite* suite) {
    suite->setupMatchAsInitiator();
    const char* matchId = suite->matchManager->getCurrentMatch()->getMatchId();
    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand ack(peerMac, QDCommand::MATCH_ID_ACK, matchId, "resp", 0, false);
    suite->matchManager->listenForMatchEvents(ack);
    EXPECT_TRUE(suite->matchManager->isMatchReady());
}
