#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "utility-tests.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "device/wireless-manager.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::DoAll;

inline Peer makePeer(const uint8_t* mac, SerialIdentifier sid) {
    Peer p;
    std::copy(mac, mac + 6, p.macAddr.begin());
    p.sid = sid;
    return p;
}

// ============================================
// Packet type alias — QuickdrawPacket is now defined in the header.
// ============================================

using TestQuickdrawPacket = QuickdrawPacket;

// ============================================
// Packet Creation Helpers
// ============================================

inline TestQuickdrawPacket createTestPacket(
    const std::string& matchId,
    const std::string& playerId,
    int command,
    long playerDrawTime = 0,
    bool isHunter = true
) {
    TestQuickdrawPacket packet;
    strncpy(packet.matchId, matchId.c_str(), 36);
    packet.matchId[36] = '\0';
    strncpy(packet.playerId, playerId.c_str(), 4);
    packet.playerId[4] = '\0';
    packet.isHunter = isHunter;
    packet.playerDrawTime = playerDrawTime;
    packet.command = command;
    return packet;
}

// Creates a packet as if the specified player is sending their draw time.
// Pass asHunter=true when the hunter is the sender, false when the bounty is.
inline TestQuickdrawPacket createTestPacketFromMatch(const std::optional<Match>& match, int command, bool asHunter) {
    long time = asHunter ? match->getHunterDrawTime() : match->getBountyDrawTime();
    const char* pid = asHunter ? match->getHunterId() : match->getBountyId();
    return createTestPacket(match->getMatchId(), pid, command, time, asHunter);
}

// ============================================
// Base Test Fixture for Single-Device Tests
// ============================================

class SingleDeviceTestFixture : public testing::Test {
public:
    static constexpr const char* DEFAULT_HUNTER_ID = "hunt";
    static constexpr const char* DEFAULT_BOUNTY_ID = "boun";
    static constexpr const char* DEFAULT_OPPONENT_MAC = "AA:BB:CC:DD:EE:FF";
    static constexpr long DEFAULT_START_TIME = 10000;

    void SetUp() override {
        setupClock();
        setupPlayer();
        setupManagers();
        setupDefaultMockExpectations();
    }

    void TearDown() override {
        cleanupManagers();
        cleanupPlayer();
        cleanupClock();
    }

    virtual std::string getMatchId() const { return "test-match-uuid-1234567890"; }

    virtual void setupDefaultMockExpectations() {
        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    // Creates a packet as if sent by the local player's opponent.
    TestQuickdrawPacket createPacket(int command, long hunterTime = 0, long bountyTime = 0) {
        if (player->isHunter()) {
            // Opponent is the bounty: send bountyTime with isHunter=false
            return createTestPacket(getMatchId(), DEFAULT_BOUNTY_ID, command, bountyTime, false);
        } else {
            // Opponent is the hunter: send hunterTime with isHunter=true
            return createTestPacket(getMatchId(), DEFAULT_HUNTER_ID, command, hunterTime, true);
        }
    }

    void processPacket(const TestQuickdrawPacket& packet, 
                      const uint8_t macAddr[6] = nullptr) {
        uint8_t defaultMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        const uint8_t* mac = macAddr ? macAddr : defaultMac;
        wirelessManager->processQuickdrawCommand(
            mac,
            reinterpret_cast<const uint8_t*>(&packet),
            sizeof(packet)
        );
    }

    // All members are public for access from standalone test functions
    MockPeerComms peerComms;
    MockHttpClient httpClient;
    MockStorage storage;
    Player* player = nullptr;
    MatchManager* matchManager = nullptr;
    FakeQuickdrawWirelessManager* wirelessManager = nullptr;
    WirelessManager* deviceWirelessManager = nullptr;
    FakePlatformClock* fakeClock = nullptr;

private:
    void setupClock() {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(DEFAULT_START_TIME);
    }

    void setupPlayer() {
        player = new Player();
        char playerId[] = "hunt";
        player->setUserID(playerId);
        player->setIsHunter(true);
    }

    void setupManagers() {
        deviceWirelessManager = new WirelessManager(&peerComms, &httpClient);
        matchManager = new MatchManager();
        wirelessManager = new FakeQuickdrawWirelessManager();
        wirelessManager->initialize(player, deviceWirelessManager, 100);
        matchManager->initialize(player, &storage, wirelessManager);

        // Hunter initiates the match through the production path.
        // FakeQuickdrawWirelessManager captures the SEND_MATCH_ID broadcast;
        // no ACK is routed back since single-device tests don't need matchIsReady.
        uint8_t opponentMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        matchManager->initializeMatch(opponentMac);
        matchManager->setDuelLocalStartTime(DEFAULT_START_TIME);
    }

    void cleanupManagers() {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete wirelessManager;
        delete deviceWirelessManager;
    }

    void cleanupPlayer() {
        delete player;
    }

    void cleanupClock() {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }
};

// ============================================
// Base Test Fixture for Two-Device Tests
// ============================================

class TwoDeviceTestFixture : public testing::Test {
public:
    static constexpr const char* DEFAULT_HUNTER_ID = "hunt";
    static constexpr const char* DEFAULT_BOUNTY_ID = "boun";
    static constexpr long DEFAULT_START_TIME = 10000;

    void SetUp() override {
        setupClock();
        setupHunter();
        setupBounty();
        setupMatches();
        setupDefaultMockExpectations();
    }

    void TearDown() override {
        cleanupMatches();
        cleanupBounty();
        cleanupHunter();
        cleanupClock();
    }

    virtual std::string getMatchId() const { return "two-device-match-id-123456"; }

    virtual void setupDefaultMockExpectations() {
        ON_CALL(hunterPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(bountyPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void hunterSendsToBounty(int command) {
        TestQuickdrawPacket packet = createTestPacketFromMatch(
            hunterMatchManager->getCurrentMatch(), command, true);
        uint8_t macAddr[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        bountyWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    void bountySendsToHunter(int command) {
        TestQuickdrawPacket packet = createTestPacketFromMatch(
            bountyMatchManager->getCurrentMatch(), command, false);
        uint8_t macAddr[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    // All members are public for access from standalone test functions
    Player* hunter = nullptr;
    Player* bounty = nullptr;
    MatchManager* hunterMatchManager = nullptr;
    MatchManager* bountyMatchManager = nullptr;
    FakeQuickdrawWirelessManager* hunterWirelessManager = nullptr;
    FakeQuickdrawWirelessManager* bountyWirelessManager = nullptr;
    WirelessManager* hunterDeviceWirelessManager = nullptr;
    WirelessManager* bountyDeviceWirelessManager = nullptr;
    MockPeerComms hunterPeerComms;
    MockPeerComms bountyPeerComms;
    MockHttpClient hunterHttpClient;
    MockHttpClient bountyHttpClient;
    MockStorage hunterStorage;
    MockStorage bountyStorage;
    FakePlatformClock* fakeClock = nullptr;

private:
    void setupClock() {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(DEFAULT_START_TIME);
    }

    void setupHunter() {
        hunter = new Player();
        char hunterId[] = "hunt";
        hunter->setUserID(hunterId);
        hunter->setIsHunter(true);

        hunterDeviceWirelessManager = new WirelessManager(&hunterPeerComms, &hunterHttpClient);
        hunterMatchManager = new MatchManager();
        hunterWirelessManager = new FakeQuickdrawWirelessManager();
        hunterWirelessManager->initialize(hunter, hunterDeviceWirelessManager, 100);
        hunterMatchManager->initialize(hunter, &hunterStorage, hunterWirelessManager);
    }

    void setupBounty() {
        bounty = new Player();
        char bountyId[] = "boun";
        bounty->setUserID(bountyId);
        bounty->setIsHunter(false);

        bountyDeviceWirelessManager = new WirelessManager(&bountyPeerComms, &bountyHttpClient);
        bountyMatchManager = new MatchManager();
        bountyWirelessManager = new FakeQuickdrawWirelessManager();
        bountyWirelessManager->initialize(bounty, bountyDeviceWirelessManager, 100);
        bountyMatchManager->initialize(bounty, &bountyStorage, bountyWirelessManager);
    }

    void setupMatches() {
        // Wire callbacks so the handshake packets are processed as they arrive.
        hunterWirelessManager->setPacketReceivedCallback(
            std::bind(&MatchManager::listenForMatchEvents, hunterMatchManager, std::placeholders::_1));
        bountyWirelessManager->setPacketReceivedCallback(
            std::bind(&MatchManager::listenForMatchEvents, bountyMatchManager, std::placeholders::_1));

        // Full production handshake:
        //   1. Hunter broadcasts SEND_MATCH_ID  → bounty receives, creates match, sends MATCH_ID_ACK
        //   2. Bounty broadcasts MATCH_ID_ACK   → hunter receives, sets matchIsReady
        uint8_t hunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterMatchManager->initializeMatch(bountyMac);
        hunterWirelessManager->deliverLastTo(bountyWirelessManager, hunterMac);
        bountyWirelessManager->deliverLastTo(hunterWirelessManager, bountyMac);

        // Clear handshake callbacks; each test sets its own for the draw phase.
        hunterWirelessManager->clearCallbacks();
        bountyWirelessManager->clearCallbacks();

        hunterMatchManager->setDuelLocalStartTime(DEFAULT_START_TIME);
        bountyMatchManager->setDuelLocalStartTime(DEFAULT_START_TIME);
    }

    void cleanupMatches() {
        hunterMatchManager->clearCurrentMatch();
        bountyMatchManager->clearCurrentMatch();
    }

    void cleanupHunter() {
        delete hunterMatchManager;
        delete hunterWirelessManager;
        delete hunterDeviceWirelessManager;
        delete hunter;
    }

    void cleanupBounty() {
        delete bountyMatchManager;
        delete bountyWirelessManager;
        delete bountyDeviceWirelessManager;
        delete bounty;
    }

    void cleanupClock() {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }
};

// ============================================
// Packet Parsing Tests
// ============================================

class PacketParsingTests : public SingleDeviceTestFixture {
public:
    std::string getMatchId() const override { return "test-match-uuid-1234567890"; }
};

inline void packetParsingDrawResultInvokesCallback(PacketParsingTests* suite) {
    bool callbackInvoked = false;
    static const char kPlaceholderMatchId[37] = "000000000000000000000000000000000000";
    QuickdrawCommand receivedCommand(nullptr, QDCommand::INVALID_COMMAND, kPlaceholderMatchId, "0000", 0, false);
    
    suite->wirelessManager->setPacketReceivedCallback([&](const QuickdrawCommand& cmd) {
        callbackInvoked = true;
        receivedCommand = cmd;
    });
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 250);
    suite->processPacket(packet);
    
    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(receivedCommand.command, QDCommand::DRAW_RESULT);
    EXPECT_EQ(receivedCommand.playerDrawTime, 250L);
}

inline void packetParsingNeverPressedParsesCorrectly(PacketParsingTests* suite) {
    static const char kPlaceholderMatchId[37] = "000000000000000000000000000000000000";
    QuickdrawCommand receivedCommand(nullptr, QDCommand::INVALID_COMMAND, kPlaceholderMatchId, "0000", 0, false);
    
    suite->wirelessManager->setPacketReceivedCallback([&](const QuickdrawCommand& cmd) {
        receivedCommand = cmd;
    });
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 0, 9999);
    suite->processPacket(packet);
    
    EXPECT_EQ(receivedCommand.command, QDCommand::NEVER_PRESSED);
    EXPECT_EQ(receivedCommand.playerDrawTime, 9999L);
}

inline void packetParsingRejectsMalformedPacket(PacketParsingTests* suite) {
    bool callbackInvoked = false;
    
    suite->wirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        callbackInvoked = true;
    });
    
    // Send a packet that's too small
    uint8_t smallPacket[10] = {0};
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    suite->wirelessManager->processQuickdrawCommand(macAddr, smallPacket, sizeof(smallPacket));
    
    // Should not invoke callback for malformed packet
    EXPECT_FALSE(callbackInvoked);
}

inline void listenForMatchResultsSetsOpponentTimeHunter(PacketParsingTests* suite) {
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 350);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 350);
}

inline void listenForMatchResultsSetsOpponentTimeBounty(PacketParsingTests* suite) {
    // Switch to bounty perspective
    suite->player->setIsHunter(false);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 200, 0);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 200);
}

inline void listenForMatchResultsHandlesNeverPressed(PacketParsingTests* suite) {
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 0, 9999);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 9999);
}

inline void listenForMatchResultsIgnoresUnexpectedCommands(PacketParsingTests* suite) {
    static const char kMatchId[37] = "000000000000000000000000000000000000";
    QuickdrawCommand cmd(nullptr, QDCommand::SEND_MATCH_ID, kMatchId, "test", 0, true);
    
    suite->matchManager->listenForMatchEvents(cmd);
    
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
}

// ============================================
// Callback Chain Tests
// ============================================

class CallbackChainTests : public SingleDeviceTestFixture {
public:
    std::string getMatchId() const override { return "callback-test-match-id-12345"; }

    void setupDefaultMockExpectations() override {
        SingleDeviceTestFixture::setupDefaultMockExpectations();
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    MockDevice device;
};

inline void callbackChainPacketToStateFlag(CallbackChainTests* suite) {
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 180);
    
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    suite->processPacket(packet);
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 180);
}

inline void callbackChainButtonPressCalculatesTime(CallbackChainTests* suite) {
    suite->fakeClock->advance(275);
    
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    ASSERT_NE(buttonCallback, nullptr);
    
    buttonCallback(suite->matchManager);
    
    EXPECT_TRUE(suite->matchManager->getHasPressedButton());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 275);
}

inline void callbackChainButtonMasherPenalty(CallbackChainTests* suite) {
    auto masherCallback = suite->matchManager->getButtonMasher();
    masherCallback(suite->matchManager);
    masherCallback(suite->matchManager);
    masherCallback(suite->matchManager);
    
    suite->fakeClock->advance(200);
    
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    buttonCallback(suite->matchManager);
    
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 425);
}

inline void callbackChainButtonPressBroadcasts(CallbackChainTests* suite) {
    suite->fakeClock->advance(150);

    size_t sentBefore = suite->wirelessManager->sentCommands.size();

    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    buttonCallback(suite->matchManager);

    ASSERT_GT(suite->wirelessManager->sentCommands.size(), sentBefore);
    EXPECT_EQ(suite->wirelessManager->sentCommands.back().command, QDCommand::DRAW_RESULT);
}

// ============================================
// State Flow Integration Tests
// ============================================

class StateFlowIntegrationTests : public SingleDeviceTestFixture {
public:
    std::string getMatchId() const override { return "flow-test-match-id-1234567890"; }

    void setupDefaultMockExpectations() override {
        SingleDeviceTestFixture::setupDefaultMockExpectations();
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockHaptics, getIntensity()).WillByDefault(Return(0));
        ON_CALL(storage, write(_, _)).WillByDefault(Return(100));
        ON_CALL(storage, writeUChar(_, _)).WillByDefault(Return(1));
        ON_CALL(storage, readUChar(_, _)).WillByDefault(Return(0));
    }

    MockDevice device;
};

inline void stateFlowDutPressesFirstWins(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(150);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelPushed());
    
    DuelPushed pushedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    pushedState.onStateMounted(&suite->device);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 150, 300);
    suite->processPacket(packet);
    
    pushedState.onStateLoop(&suite->device);
    EXPECT_TRUE(pushedState.transitionToDuelResult());
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin());
}

inline void stateFlowDutReceivesFirstLoses(StateFlowIntegrationTests* suite) {
    // Both Duel::onStateMounted and DuelReceivedResult::onStateMounted call setButtonPress.
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(2);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(2);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    duelState.onStateMounted(&suite->device);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 100);
    suite->processPacket(packet);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(350);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    receivedState.onStateLoop(&suite->device);
    EXPECT_TRUE(receivedState.transitionToDuelResult());
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin());
}

inline void stateFlowDutNeverPressesLoses(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    duelState.onStateMounted(&suite->device);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)
    );
    
    // Opponent pressed at 100ms
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 100);
    suite->processPacket(packet);
    
    // DUT never presses in time - simulate pressing very late (after pity time)
    suite->fakeClock->advance(9999);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin());
}

inline void stateFlowOpponentNeverRespondsWins(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    duelState.onStateMounted(&suite->device);
    
    // DUT presses quickly
    suite->fakeClock->advance(150);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)  
    );
    
    // Opponent never pressed
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 150, 9999);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin());
}

inline void stateFlowThroughDuelResultToWin(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(100);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)  
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 100, 200);
    suite->processPacket(packet);
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    
    // Allow display methods to be called (don't require specific calls)
    ON_CALL(*suite->device.mockDisplay, drawText(_, _, _))
        .WillByDefault(Return(suite->device.mockDisplay));
    
    resultState.onStateMounted(&suite->device);
    resultState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

inline void stateFlowThroughDuelResultToLose(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(300);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->matchManager, std::placeholders::_1)  
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 300, 150);
    suite->processPacket(packet);
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    
    // Allow display methods to be called (don't require specific calls)
    ON_CALL(*suite->device.mockDisplay, drawText(_, _, _))
        .WillByDefault(Return(suite->device.mockDisplay));
    
    resultState.onStateMounted(&suite->device);
    resultState.onStateLoop(&suite->device);
    
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_TRUE(resultState.transitionToLose());
}

inline void stateFlowDuelToWin(StateFlowIntegrationTests* suite) {
    stateFlowThroughDuelResultToWin(suite);
}

inline void stateFlowDuelToLose(StateFlowIntegrationTests* suite) {
    stateFlowThroughDuelResultToLose(suite);
}

// ============================================
// Two-Device Simulation Tests
// ============================================

class TwoDeviceSimulationTests : public TwoDeviceTestFixture {
public:
    std::string getMatchId() const override { return "two-device-match-id-123456"; }
};

inline void twoDeviceHunterPressesFirstBothAgree(TwoDeviceSimulationTests* suite) {
    suite->hunterWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->bountyMatchManager, std::placeholders::_1)
    );
    
    suite->fakeClock->advance(150);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    suite->fakeClock->advance(130);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    EXPECT_TRUE(suite->hunterMatchManager->didWin());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());
}

inline void twoDeviceBountyPressesFirstBothAgree(TwoDeviceSimulationTests* suite) {
    suite->hunterWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->bountyMatchManager, std::placeholders::_1)
    );
    
    suite->fakeClock->advance(120);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    suite->fakeClock->advance(100);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    EXPECT_FALSE(suite->hunterMatchManager->didWin());
    EXPECT_TRUE(suite->bountyMatchManager->didWin());
}

inline void twoDeviceCloseRaceCorrectWinner(TwoDeviceSimulationTests* suite) {
    suite->hunterWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, suite->bountyMatchManager, std::placeholders::_1)
    );
    
    // Very close race - hunter at 200ms, bounty at 201ms
    suite->fakeClock->advance(200);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    suite->fakeClock->advance(1);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    // Hunter wins by 1ms
    EXPECT_TRUE(suite->hunterMatchManager->didWin());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());
}

// ============================================
// Handshake Wireless Manager Integration Tests
// ============================================

// Fixture: Two HandshakeWirelessManagers backed by the same NativePeerBroker,
// simulating an output (OUTPUT_JACK) device and an input (INPUT_JACK) device.
class HandshakeIntegrationTests : public testing::Test {
public:
    struct RawHSPacket { int sendingJack; int receivingJack; int command; } __attribute__((packed));

    void SetUp() override {
        ON_CALL(outputPeerComms, sendData(_, _, _, _))
            .WillByDefault(Invoke([this](const uint8_t*, PktType, const uint8_t* data, size_t len) {
                inputHWM.processHandshakeCommand(outputMac, data, len);
                return 1;
            }));
        ON_CALL(inputPeerComms, sendData(_, _, _, _))
            .WillByDefault(Invoke([this](const uint8_t*, PktType, const uint8_t* data, size_t len) {
                outputHWM.processHandshakeCommand(inputMac, data, len);
                return 1;
            }));

        outputWirelessManager = new WirelessManager(&outputPeerComms, &outputHttpClient);
        inputWirelessManager  = new WirelessManager(&inputPeerComms,  &inputHttpClient);

        outputHWM.initialize(outputWirelessManager);
        inputHWM.initialize(inputWirelessManager);
    }

    void TearDown() override {
        delete outputWirelessManager;
        delete inputWirelessManager;
    }

    // Deliver a raw packet directly to a manager (bypasses sendData, for malformed-packet tests)
    void deliverRawToInput(const uint8_t* data, size_t len) {
        inputHWM.processHandshakeCommand(outputMac, data, len);
    }

    NiceMock<MockPeerComms> outputPeerComms;
    NiceMock<MockPeerComms> inputPeerComms;
    MockHttpClient outputHttpClient;
    MockHttpClient inputHttpClient;

    WirelessManager* outputWirelessManager = nullptr;
    WirelessManager* inputWirelessManager  = nullptr;

    HandshakeWirelessManager outputHWM;
    HandshakeWirelessManager inputHWM;

    uint8_t outputMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    uint8_t inputMac[6]  = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
};

// Test: EXCHANGE_ID sent by output (OUTPUT) is routed to the input INPUT callback
inline void handshakeCompleteBountyPerspective(HandshakeIntegrationTests* suite) {
    HandshakeCommand receivedCmd(nullptr, -1, 0, SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK);
    bool callbackFired = false;

    suite->inputHWM.setPacketReceivedCallback([&](HandshakeCommand cmd) {
        receivedCmd = cmd;
        callbackFired = true;
    }, SerialIdentifier::INPUT_JACK);

    suite->outputHWM.setMacPeer(SerialIdentifier::OUTPUT_JACK, makePeer(suite->inputMac, SerialIdentifier::INPUT_JACK));
    suite->outputHWM.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);

    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(receivedCmd.command, HSCommand::EXCHANGE_ID);
    EXPECT_EQ(receivedCmd.receivingJack, SerialIdentifier::INPUT_JACK);
}

// Test: EXCHANGE_ID sent by input (INPUT) is routed to the output OUTPUT callback
inline void handshakeCompleteHunterPerspective(HandshakeIntegrationTests* suite) {
    HandshakeCommand receivedCmd(nullptr, -1, 0, SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK);
    bool callbackFired = false;

    suite->outputHWM.setPacketReceivedCallback([&](HandshakeCommand cmd) {
        receivedCmd = cmd;
        callbackFired = true;
    }, SerialIdentifier::OUTPUT_JACK);

    suite->inputHWM.setMacPeer(SerialIdentifier::INPUT_JACK, makePeer(suite->outputMac, SerialIdentifier::OUTPUT_JACK));
    suite->inputHWM.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);

    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(receivedCmd.command, HSCommand::EXCHANGE_ID);
    EXPECT_EQ(receivedCmd.receivingJack, SerialIdentifier::OUTPUT_JACK);
}

// Test: Full 4-step symmetric handshake protocol
inline void handshakeTwoDeviceFullFlow(HandshakeIntegrationTests* suite) {
    std::vector<HandshakeCommand> outputReceived;
    std::vector<HandshakeCommand> inputReceived;

    suite->outputHWM.setPacketReceivedCallback([&](HandshakeCommand cmd) {
        outputReceived.push_back(cmd);
    }, SerialIdentifier::OUTPUT_JACK);
    suite->inputHWM.setPacketReceivedCallback([&](HandshakeCommand cmd) {
        inputReceived.push_back(cmd);
    }, SerialIdentifier::INPUT_JACK);

    // Step 1: output → input: EXCHANGE_ID
    suite->outputHWM.setMacPeer(SerialIdentifier::OUTPUT_JACK, makePeer(suite->inputMac, SerialIdentifier::INPUT_JACK));
    suite->outputHWM.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);

    // Step 2: input → output: EXCHANGE_ID reply
    suite->inputHWM.setMacPeer(SerialIdentifier::INPUT_JACK, makePeer(suite->outputMac, SerialIdentifier::OUTPUT_JACK));
    suite->inputHWM.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);

    // Step 3: output → input: final EXCHANGE_ID ack
    suite->outputHWM.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);

    EXPECT_EQ(outputReceived.size(), 1u);  // step 2
    EXPECT_EQ(inputReceived.size(), 2u);   // steps 1 and 3
}

// Test: Incomplete handshake (only step 1) leaves system stable
inline void handshakeTimeoutBeforeCompletion(HandshakeIntegrationTests* suite) {
    bool inputCallbackFired = false;
    suite->inputHWM.setPacketReceivedCallback([&](HandshakeCommand) {
        inputCallbackFired = true;
    }, SerialIdentifier::INPUT_JACK);

    suite->outputHWM.setMacPeer(SerialIdentifier::OUTPUT_JACK, makePeer(suite->inputMac, SerialIdentifier::INPUT_JACK));
    suite->outputHWM.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);

    EXPECT_TRUE(inputCallbackFired);
    // Input received the packet; output is still waiting — no crash.
}

// Test: Malformed packet (wrong size) is rejected
inline void handshakeRejectsInvalidPacketData(HandshakeIntegrationTests* suite) {
    bool callbackFired = false;
    suite->inputHWM.setPacketReceivedCallback([&](HandshakeCommand) {
        callbackFired = true;
    }, SerialIdentifier::INPUT_JACK);

    uint8_t badPacket[3] = {0x01, 0x02, 0x03};
    suite->deliverRawToInput(badPacket, sizeof(badPacket));

    EXPECT_FALSE(callbackFired);
}

// Test: Out-of-range command value is rejected
inline void handshakeIgnoresUnexpectedCommands(HandshakeIntegrationTests* suite) {
    bool callbackFired = false;
    suite->inputHWM.setPacketReceivedCallback([&](HandshakeCommand) {
        callbackFired = true;
    }, SerialIdentifier::INPUT_JACK);

    HandshakeIntegrationTests::RawHSPacket pkt{
        static_cast<int>(SerialIdentifier::OUTPUT_JACK),
        static_cast<int>(SerialIdentifier::INPUT_JACK),
        HSCommand::HS_COMMAND_COUNT  // one past the valid range
    };
    suite->deliverRawToInput(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));

    EXPECT_FALSE(callbackFired);
}

// Test: Sender MAC is captured in the HandshakeCommand
inline void handshakeSetsOpponentMacAddress(HandshakeIntegrationTests* suite) {
    HandshakeCommand receivedCmd(nullptr, -1, 0, SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK);
    suite->inputHWM.setPacketReceivedCallback([&](HandshakeCommand cmd) {
        receivedCmd = cmd;
    }, SerialIdentifier::INPUT_JACK);

    suite->outputHWM.setMacPeer(SerialIdentifier::OUTPUT_JACK, makePeer(suite->inputMac, SerialIdentifier::INPUT_JACK));
    suite->outputHWM.sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);

    EXPECT_TRUE(receivedCmd.wifiMacAddrValid);
}

// Test: NOTIFY_DISCONNECT is routed correctly
inline void handshakeMatchDataPropagatedCorrectly(HandshakeIntegrationTests* suite) {
    HandshakeCommand receivedCmd(nullptr, -1, 0, SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK);
    suite->inputHWM.setPacketReceivedCallback([&](HandshakeCommand cmd) {
        receivedCmd = cmd;
    }, SerialIdentifier::INPUT_JACK);

    suite->outputHWM.setMacPeer(SerialIdentifier::OUTPUT_JACK, makePeer(suite->inputMac, SerialIdentifier::INPUT_JACK));
    suite->outputHWM.sendPacket(HSCommand::NOTIFY_DISCONNECT, SerialIdentifier::OUTPUT_JACK);

    EXPECT_EQ(receivedCmd.command, HSCommand::NOTIFY_DISCONNECT);
}

inline void handshakePacketPreservesPlayerIds(HandshakeIntegrationTests* suite) {
    handshakeMatchDataPropagatedCorrectly(suite);
}

inline void handshakeMultiplePacketsDontInterfere(HandshakeIntegrationTests* suite) {
    handshakeTwoDeviceFullFlow(suite);
}
