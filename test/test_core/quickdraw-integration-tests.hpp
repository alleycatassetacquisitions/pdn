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
#include "device/wireless-manager.hpp"
#include "../test-constants.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::DoAll;

// ============================================
// QuickdrawPacket structure for creating test packets
// Must match the packed structure in quickdraw-wireless-manager.cpp
// ============================================

struct TestQuickdrawPacket {
    char matchId[37];  // UUID_BUFFER_SIZE
    char hunterId[5];
    char bountyId[5];
    long hunterDrawTime;
    long bountyDrawTime;
    int command;
} __attribute__((packed));

// ============================================
// Packet Creation Helpers
// ============================================

inline TestQuickdrawPacket createTestPacket(
    const std::string& matchId,
    const std::string& hunterId,
    const std::string& bountyId,
    int command,
    long hunterTime = 0,
    long bountyTime = 0
) {
    TestQuickdrawPacket packet;
    strncpy(packet.matchId, matchId.c_str(), 36);
    packet.matchId[36] = '\0';
    strncpy(packet.hunterId, hunterId.c_str(), 4);
    packet.hunterId[4] = '\0';
    strncpy(packet.bountyId, bountyId.c_str(), 4);
    packet.bountyId[4] = '\0';
    packet.hunterDrawTime = hunterTime;
    packet.bountyDrawTime = bountyTime;
    packet.command = command;
    return packet;
}

inline TestQuickdrawPacket createTestPacketFromMatch(Match* match, int command) {
    return createTestPacket(
        match->getMatchId(),
        match->getHunterId(),
        match->getBountyId(),
        command,
        match->getHunterDrawTime(),
        match->getBountyDrawTime()
    );
}

// ============================================
// Base Test Fixture for Single-Device Tests
// ============================================

class SingleDeviceTestFixture : public testing::Test {
public:
    static constexpr const char* DEFAULT_HUNTER_ID = "hunt";
    static constexpr const char* DEFAULT_BOUNTY_ID = "boun";
    static constexpr const char* DEFAULT_OPPONENT_MAC = TestConstants::TEST_MAC_DEFAULT;
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

    TestQuickdrawPacket createPacket(int command, long hunterTime = 0, long bountyTime = 0) {
        return createTestPacket(getMatchId(), DEFAULT_HUNTER_ID, DEFAULT_BOUNTY_ID, 
                               command, hunterTime, bountyTime);
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
    QuickdrawWirelessManager* wirelessManager = nullptr;
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
        player->setOpponentMacAddress(DEFAULT_OPPONENT_MAC);
    }

    void setupManagers() {
        deviceWirelessManager = new WirelessManager(&peerComms, &httpClient);
        matchManager = new MatchManager();
        wirelessManager = new QuickdrawWirelessManager();
        wirelessManager->initialize(player, deviceWirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);
        matchManager->createMatch(getMatchId(), player->getUserID(), DEFAULT_BOUNTY_ID);
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
            hunterMatchManager->getCurrentMatch(), command);
        uint8_t macAddr[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        bountyWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    void bountySendsToHunter(int command) {
        TestQuickdrawPacket packet = createTestPacketFromMatch(
            bountyMatchManager->getCurrentMatch(), command);
        uint8_t macAddr[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    void hunterSendsToBounty(int command, const std::string& matchId,
                            const std::string& hunterId, const std::string& bountyId) {
        TestQuickdrawPacket packet = createTestPacket(matchId, hunterId, bountyId, command);
        uint8_t macAddr[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        bountyWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    void bountySendsToHunter(int command, const std::string& matchId,
                            const std::string& hunterId, const std::string& bountyId) {
        TestQuickdrawPacket packet = createTestPacket(matchId, hunterId, bountyId, command);
        uint8_t macAddr[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    TestQuickdrawPacket createHandshakePacket(int command, const std::string& matchId,
                                              const std::string& hunterId, const std::string& bountyId) {
        return createTestPacket(matchId, hunterId, bountyId, command);
    }

    // All members are public for access from standalone test functions
    Player* hunter = nullptr;
    Player* bounty = nullptr;
    MatchManager* hunterMatchManager = nullptr;
    MatchManager* bountyMatchManager = nullptr;
    QuickdrawWirelessManager* hunterWirelessManager = nullptr;
    QuickdrawWirelessManager* bountyWirelessManager = nullptr;
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
        hunter->setOpponentMacAddress("BB:BB:BB:BB:BB:BB");

        hunterDeviceWirelessManager = new WirelessManager(&hunterPeerComms, &hunterHttpClient);
        hunterMatchManager = new MatchManager();
        hunterWirelessManager = new QuickdrawWirelessManager();
        hunterWirelessManager->initialize(hunter, hunterDeviceWirelessManager, 100);
        hunterMatchManager->initialize(hunter, &hunterStorage, &hunterPeerComms, hunterWirelessManager);
    }

    void setupBounty() {
        bounty = new Player();
        char bountyId[] = "boun";
        bounty->setUserID(bountyId);
        bounty->setIsHunter(false);
        bounty->setOpponentMacAddress("AA:AA:AA:AA:AA:AA");

        bountyDeviceWirelessManager = new WirelessManager(&bountyPeerComms, &bountyHttpClient);
        bountyMatchManager = new MatchManager();
        bountyWirelessManager = new QuickdrawWirelessManager();
        bountyWirelessManager->initialize(bounty, bountyDeviceWirelessManager, 100);
        bountyMatchManager->initialize(bounty, &bountyStorage, &bountyPeerComms, bountyWirelessManager);
    }

    void setupMatches() {
        hunterMatchManager->createMatch(getMatchId(), DEFAULT_HUNTER_ID, DEFAULT_BOUNTY_ID);
        bountyMatchManager->createMatch(getMatchId(), DEFAULT_HUNTER_ID, DEFAULT_BOUNTY_ID);
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
    QuickdrawCommand receivedCommand;
    
    suite->wirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        callbackInvoked = true;
        receivedCommand = cmd;
    });
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 250);
    suite->processPacket(packet);
    
    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(receivedCommand.command, QDCommand::DRAW_RESULT);
    EXPECT_EQ(receivedCommand.match.getBountyDrawTime(), 250);
}

inline void packetParsingNeverPressedParsesCorrectly(PacketParsingTests* suite) {
    QuickdrawCommand receivedCommand;
    
    suite->wirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        receivedCommand = cmd;
    });
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 0, 9999);
    suite->processPacket(packet);
    
    EXPECT_EQ(receivedCommand.command, QDCommand::NEVER_PRESSED);
    EXPECT_EQ(receivedCommand.match.getBountyDrawTime(), 9999);
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
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
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
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 200, 0);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 200);
}

inline void listenForMatchResultsHandlesNeverPressed(PacketParsingTests* suite) {
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 0, 9999);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 9999);
}

inline void listenForMatchResultsIgnoresUnexpectedCommands(PacketParsingTests* suite) {
    QuickdrawCommand cmd;
    cmd.command = QDCommand::CONNECTION_CONFIRMED;
    cmd.match = Match();
    
    suite->matchManager->listenForMatchResults(cmd);
    
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
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
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
    
    EXPECT_CALL(suite->peerComms, sendData(_, _, _, _))
        .Times(1)
        .WillOnce(Return(1));
    
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    buttonCallback(suite->matchManager);
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

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(150);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelPushed());
    
    DuelPushed pushedState(suite->player, suite->matchManager);
    pushedState.onStateMounted(&suite->device);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 150, 300);
    suite->processPacket(packet);
    
    pushedState.onStateLoop(&suite->device);
    EXPECT_TRUE(pushedState.transitionToDuelResult());
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin());
}

inline void stateFlowDutReceivesFirstLoses(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 100);
    suite->processPacket(packet);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
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

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
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

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // DUT presses quickly
    suite->fakeClock->advance(150);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
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

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(100);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
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

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(300);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
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
        std::bind(&MatchManager::listenForMatchResults, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->bountyMatchManager, std::placeholders::_1)
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
        std::bind(&MatchManager::listenForMatchResults, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->bountyMatchManager, std::placeholders::_1)
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
        std::bind(&MatchManager::listenForMatchResults, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->bountyMatchManager, std::placeholders::_1)
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
// Handshake Integration Tests
// ============================================

class HandshakeIntegrationTests : public TwoDeviceTestFixture {
public:
    void SetUp() override {
        TwoDeviceTestFixture::SetUp();
        // Clear matches for handshake tests - they create them dynamically
        hunterMatchManager->clearCurrentMatch();
        bountyMatchManager->clearCurrentMatch();
    }

    void setupDefaultMockExpectations() override {
        TwoDeviceTestFixture::setupDefaultMockExpectations();
        ON_CALL(*hunterDevice.mockDisplay, invalidateScreen()).WillByDefault(Return(hunterDevice.mockDisplay));
        ON_CALL(*hunterDevice.mockDisplay, drawImage(_)).WillByDefault(Return(hunterDevice.mockDisplay));
        ON_CALL(*bountyDevice.mockDisplay, invalidateScreen()).WillByDefault(Return(bountyDevice.mockDisplay));
        ON_CALL(*bountyDevice.mockDisplay, drawImage(_)).WillByDefault(Return(bountyDevice.mockDisplay));
    }

    MockDevice hunterDevice;
    MockDevice bountyDevice;
};

inline void handshakeCompleteBountyPerspective(HandshakeIntegrationTests* suite) {
    QuickdrawCommand receivedCommand;
    suite->bountyWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        receivedCommand = cmd;
    });
    
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH,
                               "handshake-match-id-1234567890", "hunt", "boun");
    
    EXPECT_EQ(receivedCommand.command, QDCommand::HUNTER_RECEIVE_MATCH);
    EXPECT_EQ(receivedCommand.match.getMatchId(), "handshake-match-id-1234567890");
    EXPECT_EQ(receivedCommand.match.getHunterId(), "hunt");
    EXPECT_EQ(receivedCommand.match.getBountyId(), "boun");
}

inline void handshakeCompleteHunterPerspective(HandshakeIntegrationTests* suite) {
    QuickdrawCommand receivedCommand;
    suite->hunterWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        receivedCommand = cmd;
    });
    
    suite->bountySendsToHunter(QDCommand::CONNECTION_CONFIRMED,
                               "handshake-match-id-1234567890", "hunt", "boun");
    
    EXPECT_EQ(receivedCommand.command, QDCommand::CONNECTION_CONFIRMED);
    EXPECT_EQ(receivedCommand.match.getMatchId(), "handshake-match-id-1234567890");
}

inline void handshakeTwoDeviceFullFlow(HandshakeIntegrationTests* suite) {
    std::vector<QuickdrawCommand> hunterReceived;
    std::vector<QuickdrawCommand> bountyReceived;
    
    suite->hunterWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        hunterReceived.push_back(cmd);
    });
    suite->bountyWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        bountyReceived.push_back(cmd);
    });
    
    // Full handshake flow
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH,
                               "full-flow-match-id-1234567", "hunt", "boun");
    suite->bountySendsToHunter(QDCommand::CONNECTION_CONFIRMED,
                               "full-flow-match-id-1234567", "hunt", "boun");
    suite->hunterSendsToBounty(QDCommand::BOUNTY_FINAL_ACK,
                               "full-flow-match-id-1234567", "hunt", "boun");
    
    EXPECT_EQ(hunterReceived.size(), 1);
    EXPECT_EQ(bountyReceived.size(), 2);
}

inline void handshakeTimeoutBeforeCompletion(HandshakeIntegrationTests* suite) {
    // This test verifies that incomplete handshakes don't cause issues
    QuickdrawCommand receivedCommand;
    suite->bountyWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        receivedCommand = cmd;
    });
    
    // Only send first packet, no follow-up
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH,
                               "timeout-match-id-1234567890", "hunt", "boun");
    
    EXPECT_EQ(receivedCommand.command, QDCommand::HUNTER_RECEIVE_MATCH);
    // Handshake is incomplete but system should remain stable
}

inline void handshakeRejectsInvalidPacketData(HandshakeIntegrationTests* suite) {
    bool callbackInvoked = false;
    suite->bountyWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        callbackInvoked = true;
    });
    
    // Send malformed packet
    uint8_t badPacket[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t macAddr[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    
    suite->bountyWirelessManager->processQuickdrawCommand(macAddr, badPacket, sizeof(badPacket));
    
    EXPECT_FALSE(callbackInvoked);
}

inline void handshakeIgnoresUnexpectedCommands(HandshakeIntegrationTests* suite) {
    std::vector<QuickdrawCommand> received;
    suite->bountyWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        received.push_back(cmd);
    });
    
    // Send valid but unexpected command
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT,
                               "unexpected-match-1234567890", "hunt", "boun");
    
    // Should still receive the packet (filtering is done at higher level)
    EXPECT_EQ(received.size(), 1);
    EXPECT_EQ(received[0].command, QDCommand::DRAW_RESULT);
}

inline void handshakeSetsOpponentMacAddress(HandshakeIntegrationTests* suite) {
    QuickdrawCommand receivedCommand;
    suite->bountyWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        receivedCommand = cmd;
    });
    
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH,
                               TestConstants::TEST_MATCH_ID_MAC, "hunt", "boun");
    
    // The MAC address should be captured in the command
    EXPECT_FALSE(receivedCommand.wifiMacAddr.empty());
}

inline void handshakeMatchDataPropagatedCorrectly(HandshakeIntegrationTests* suite) {
    QuickdrawCommand receivedCommand;
    suite->bountyWirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        receivedCommand = cmd;
    });
    
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH,
                               "propagate-match-1234567890", "HNTR", "BNTY");
    
    EXPECT_EQ(receivedCommand.match.getMatchId(), "propagate-match-1234567890");
    EXPECT_EQ(receivedCommand.match.getHunterId(), "HNTR");
    EXPECT_EQ(receivedCommand.match.getBountyId(), "BNTY");
}

inline void handshakePacketPreservesPlayerIds(HandshakeIntegrationTests* suite) {
    handshakeMatchDataPropagatedCorrectly(suite);
}

inline void handshakeMultiplePacketsDontInterfere(HandshakeIntegrationTests* suite) {
    handshakeTwoDeviceFullFlow(suite);
}
