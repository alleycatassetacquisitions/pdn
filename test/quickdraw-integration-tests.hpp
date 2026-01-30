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
// Packet Parsing Tests
// Tests for MatchManager::listenForMatchResults() and 
// QuickdrawWirelessManager::processQuickdrawCommand()
// ============================================

class PacketParsingTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        player = new Player();
        char playerId[] = "hunt";
        player->setUserID(playerId);
        player->setIsHunter(true);
        player->setOpponentMacAddress("AA:BB:CC:DD:EE:FF");

        matchManager = new MatchManager();
        wirelessManager = new QuickdrawWirelessManager();
        wirelessManager->initialize(player, &peerComms, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        // Create a match
        matchManager->createMatch("test-match-uuid-1234567890", player->getUserID(), "boun");
        matchManager->setDuelLocalStartTime(10000);

        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Helper: Create a test packet with specified values
    TestQuickdrawPacket createPacket(int command, long hunterTime, long bountyTime) {
        TestQuickdrawPacket packet;
        strncpy(packet.matchId, "test-match-uuid-1234567890", 36);
        packet.matchId[36] = '\0';
        strncpy(packet.hunterId, "hunt", 4);
        packet.hunterId[4] = '\0';
        strncpy(packet.bountyId, "boun", 4);
        packet.bountyId[4] = '\0';
        packet.hunterDrawTime = hunterTime;
        packet.bountyDrawTime = bountyTime;
        packet.command = command;
        return packet;
    }

    MockPeerComms peerComms;
    MockStorage storage;
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* wirelessManager;
    FakePlatformClock* fakeClock;
};

// Test: processQuickdrawCommand parses DRAW_RESULT and invokes callback
inline void packetParsingDrawResultInvokesCallback(PacketParsingTests* suite) {
    bool callbackInvoked = false;
    QuickdrawCommand receivedCommand;
    
    // Set up callback to capture the parsed command
    suite->wirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        callbackInvoked = true;
        receivedCommand = cmd;
    });
    
    // Create and send a DRAW_RESULT packet
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 250);
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    int result = suite->wirelessManager->processQuickdrawCommand(
        macAddr, 
        reinterpret_cast<uint8_t*>(&packet), 
        sizeof(packet)
    );
    
    EXPECT_EQ(result, 1);
    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(receivedCommand.command, QDCommand::DRAW_RESULT);
    EXPECT_EQ(receivedCommand.match.getBountyDrawTime(), 250);
}

// Test: processQuickdrawCommand parses NEVER_PRESSED correctly
inline void packetParsingNeverPressedParsesCorrectly(PacketParsingTests* suite) {
    QuickdrawCommand receivedCommand;
    
    suite->wirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        receivedCommand = cmd;
    });
    
    // NEVER_PRESSED packet with bounty pity time
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 0, 800);
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    suite->wirelessManager->processQuickdrawCommand(
        macAddr, 
        reinterpret_cast<uint8_t*>(&packet), 
        sizeof(packet)
    );
    
    EXPECT_EQ(receivedCommand.command, QDCommand::NEVER_PRESSED);
    EXPECT_EQ(receivedCommand.match.getBountyDrawTime(), 800);
}

// Test: processQuickdrawCommand rejects malformed packets
inline void packetParsingRejectsMalformedPacket(PacketParsingTests* suite) {
    bool callbackInvoked = false;
    suite->wirelessManager->setPacketReceivedCallback([&](QuickdrawCommand cmd) {
        callbackInvoked = true;
    });
    
    // Send packet with wrong size
    uint8_t badData[10] = {0};
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    int result = suite->wirelessManager->processQuickdrawCommand(
        macAddr, badData, sizeof(badData)
    );
    
    EXPECT_EQ(result, -1);
    EXPECT_FALSE(callbackInvoked);
}

// Test: listenForMatchResults sets opponent draw time correctly (Hunter perspective)
inline void listenForMatchResultsSetsOpponentTimeHunter(PacketParsingTests* suite) {
    // Player is hunter, receiving bounty's time
    suite->player->setIsHunter(true);
    
    Match opponentMatch("test-match-uuid-1234567890", "hunt", "boun");
    opponentMatch.setBountyDrawTime(175);
    
    QuickdrawCommand cmd;
    cmd.command = QDCommand::DRAW_RESULT;
    cmd.match = opponentMatch;
    
    suite->matchManager->listenForMatchResults(cmd);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 175);
}

// Test: listenForMatchResults sets opponent draw time correctly (Bounty perspective)
inline void listenForMatchResultsSetsOpponentTimeBounty(PacketParsingTests* suite) {
    // Switch player to bounty
    suite->player->setIsHunter(false);
    
    Match opponentMatch("test-match-uuid-1234567890", "hunt", "boun");
    opponentMatch.setHunterDrawTime(200);
    
    QuickdrawCommand cmd;
    cmd.command = QDCommand::DRAW_RESULT;
    cmd.match = opponentMatch;
    
    suite->matchManager->listenForMatchResults(cmd);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 200);
}

// Test: listenForMatchResults handles NEVER_PRESSED command
inline void listenForMatchResultsHandlesNeverPressed(PacketParsingTests* suite) {
    suite->player->setIsHunter(true);
    
    Match opponentMatch("test-match-uuid-1234567890", "hunt", "boun");
    opponentMatch.setBountyDrawTime(850); // Pity time
    
    QuickdrawCommand cmd;
    cmd.command = QDCommand::NEVER_PRESSED;
    cmd.match = opponentMatch;
    
    suite->matchManager->listenForMatchResults(cmd);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 850);
}

// Test: listenForMatchResults ignores unexpected commands
inline void listenForMatchResultsIgnoresUnexpectedCommands(PacketParsingTests* suite) {
    QuickdrawCommand cmd;
    cmd.command = QDCommand::CONNECTION_CONFIRMED;
    cmd.match = Match();
    
    suite->matchManager->listenForMatchResults(cmd);
    
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
}

// ============================================
// Callback Chain Tests
// Tests that verify the full callback flow from 
// wireless manager through match manager to state transitions
// ============================================

class CallbackChainTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        player = new Player();
        char playerId[] = "hunt";
        player->setUserID(playerId);
        player->setIsHunter(true);
        player->setOpponentMacAddress("AA:BB:CC:DD:EE:FF");

        matchManager = new MatchManager();
        wirelessManager = new QuickdrawWirelessManager();
        wirelessManager->initialize(player, &peerComms, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        matchManager->createMatch("callback-test-match-id-12345", player->getUserID(), "boun");
        matchManager->setDuelLocalStartTime(10000);

        // Set up default mock expectations
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    TestQuickdrawPacket createPacket(int command, long hunterTime, long bountyTime) {
        TestQuickdrawPacket packet;
        strncpy(packet.matchId, "callback-test-match-id-12345", 36);
        packet.matchId[36] = '\0';
        strncpy(packet.hunterId, "hunt", 4);
        packet.hunterId[4] = '\0';
        strncpy(packet.bountyId, "boun", 4);
        packet.bountyId[4] = '\0';
        packet.hunterDrawTime = hunterTime;
        packet.bountyDrawTime = bountyTime;
        packet.command = command;
        return packet;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* wirelessManager;
    FakePlatformClock* fakeClock;
};

// Test: Full callback chain - packet arrives, callback invoked, state flag set
inline void callbackChainPacketToStateFlag(CallbackChainTests* suite) {
    // Wire up the callback as the Duel state does
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
    );
    
    // Simulate incoming packet
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 180);
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    // Before packet: no result received
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    
    // Process packet (this triggers the full chain)
    suite->wirelessManager->processQuickdrawCommand(
        macAddr, 
        reinterpret_cast<uint8_t*>(&packet), 
        sizeof(packet)
    );
    
    // After packet: result received flag should be set
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 180);
}

// Test: Button press callback calculates reaction time correctly
inline void callbackChainButtonPressCalculatesTime(CallbackChainTests* suite) {
    // Advance clock to simulate reaction time
    suite->fakeClock->advance(275); // 275ms reaction time
    
    // Get and invoke the button callback
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    ASSERT_NE(buttonCallback, nullptr);
    
    buttonCallback(suite->matchManager);
    
    // Verify reaction time was calculated and stored
    EXPECT_TRUE(suite->matchManager->getHasPressedButton());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 275);
}

// Test: Button press callback applies button masher penalty
inline void callbackChainButtonMasherPenalty(CallbackChainTests* suite) {
    // Simulate button mashing during countdown
    auto masherCallback = suite->matchManager->getButtonMasher();
    masherCallback(suite->matchManager);
    masherCallback(suite->matchManager);
    masherCallback(suite->matchManager); // 3 early presses
    
    // Advance clock for reaction
    suite->fakeClock->advance(200);
    
    // Press button during duel
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    buttonCallback(suite->matchManager);
    
    // Reaction time should be 200 + (3 * 75) = 425ms
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 425);
}

// Test: Button press triggers wireless broadcast
inline void callbackChainButtonPressBroadcasts(CallbackChainTests* suite) {
    suite->fakeClock->advance(150);
    
    // Expect sendData to be called
    EXPECT_CALL(suite->peerComms, sendData(_, _, _, _))
        .Times(1)
        .WillOnce(Return(1));
    
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    buttonCallback(suite->matchManager);
}

// ============================================
// Full State Flow Integration Tests
// Tests complete flows through the state machine
// using proper event triggering
// ============================================

class StateFlowIntegrationTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        player = new Player();
        char playerId[] = "hunt";
        player->setUserID(playerId);
        player->setIsHunter(true);
        player->setOpponentMacAddress("AA:BB:CC:DD:EE:FF");

        matchManager = new MatchManager();
        wirelessManager = new QuickdrawWirelessManager();
        wirelessManager->initialize(player, &peerComms, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        matchManager->createMatch("flow-test-match-id-1234567890", player->getUserID(), "boun");

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockHaptics, getIntensity()).WillByDefault(Return(0));
        ON_CALL(peerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(storage, write(_, _)).WillByDefault(Return(100));
        ON_CALL(storage, writeUChar(_, _)).WillByDefault(Return(1));
        ON_CALL(storage, readUChar(_, _)).WillByDefault(Return(0));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    TestQuickdrawPacket createPacket(int command, long hunterTime, long bountyTime) {
        TestQuickdrawPacket packet;
        strncpy(packet.matchId, "flow-test-match-id-1234567890", 36);
        packet.matchId[36] = '\0';
        strncpy(packet.hunterId, "hunt", 4);
        packet.hunterId[4] = '\0';
        strncpy(packet.bountyId, "boun", 4);
        packet.bountyId[4] = '\0';
        packet.hunterDrawTime = hunterTime;
        packet.bountyDrawTime = bountyTime;
        packet.command = command;
        return packet;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* wirelessManager;
    FakePlatformClock* fakeClock;
};

// Test: Complete flow - DUT presses first, then receives opponent result → Win
inline void stateFlowDutPressesFirstWins(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // Create Duel state
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // DUT presses button (fast - 150ms)
    suite->fakeClock->advance(150);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelPushed());
    
    // Transition to DuelPushed
    DuelPushed pushedState(suite->player, suite->matchManager);
    pushedState.onStateMounted(&suite->device);
    
    // Receive opponent result (slower - 300ms) via callback chain
    suite->wirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->matchManager, std::placeholders::_1)
    );
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 150, 300);
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->wirelessManager->processQuickdrawCommand(
        macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
    );
    
    pushedState.onStateLoop(&suite->device);
    EXPECT_TRUE(pushedState.transitionToDuelResult());
    
    // Verify win condition
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin()); // Hunter 150ms < Bounty 300ms
}

// Test: Complete flow - DUT receives result first, then presses → Lose
inline void stateFlowDutReceivesFirstLoses(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // Create Duel state with callback chain wired up
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // Receive opponent result first (fast - 120ms)
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 120);
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->wirelessManager->processQuickdrawCommand(
        macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
    );
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
    
    // Transition to DuelReceivedResult
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);
    
    // DUT presses button (slower - 350ms)
    suite->fakeClock->advance(350);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    receivedState.onStateLoop(&suite->device);
    EXPECT_TRUE(receivedState.transitionToDuelResult());
    
    // Verify loss condition
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin()); // Hunter 350ms > Bounty 120ms
}

// Test: Complete flow - DUT never presses, grace period expires → Lose
inline void stateFlowDutNeverPressesLoses(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // Create Duel state
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // Receive opponent result
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 150);
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->wirelessManager->processQuickdrawCommand(
        macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
    );
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
    
    // Transition to DuelReceivedResult
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);
    
    // DUT never presses - advance past grace period (750ms + buffer)
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    // Should transition with pity time set
    EXPECT_TRUE(receivedState.transitionToDuelResult());
    
    // Hunter draw time should be set to pity time (roughly 800ms from duel start)
    // The actual pity time is: currentTime - duelLocalStartTime = 10800 - 10000 = 800
    EXPECT_GT(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 0);
    
    // Verify loss (pity time > opponent time)
    EXPECT_FALSE(suite->matchManager->didWin());
}

// Test: Complete flow - DUT presses, opponent never responds → Win
inline void stateFlowOpponentNeverRespondsWins(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // Create Duel state
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // DUT presses button (200ms)
    suite->fakeClock->advance(200);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelPushed());
    
    // Transition to DuelPushed
    DuelPushed pushedState(suite->player, suite->matchManager);
    pushedState.onStateMounted(&suite->device);
    
    // Opponent never responds - advance past grace period (900ms + buffer)
    suite->fakeClock->advance(1000);
    pushedState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(pushedState.transitionToDuelResult());
    
    // Set never pressed for opponent (bounty draw time stays 0)
    suite->matchManager->setNeverPressed();
    
    // Hunter wins because bounty never pressed (time = 0 is treated as no press)
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Complete flow through DuelResult to Win state
inline void stateFlowThroughDuelResultToWin(StateFlowIntegrationTests* suite) {
    // Set up winning scenario
    suite->matchManager->setHunterDrawTime(150);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin());
    
    // Create DuelResult state
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    
    EXPECT_CALL(*suite->device.mockDisplay, drawText(_, _, _))
        .WillRepeatedly(Return(suite->device.mockDisplay));
    
    resultState.onStateMounted(&suite->device);
    resultState.onStateLoop(&suite->device);
    
    // Should transition to win
    EXPECT_TRUE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

// Test: Complete flow through DuelResult to Lose state  
inline void stateFlowThroughDuelResultToLose(StateFlowIntegrationTests* suite) {
    // Set up losing scenario
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setBountyDrawTime(180);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin());
    
    // Create DuelResult state
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    
    EXPECT_CALL(*suite->device.mockDisplay, drawText(_, _, _))
        .WillRepeatedly(Return(suite->device.mockDisplay));
    
    resultState.onStateMounted(&suite->device);
    resultState.onStateLoop(&suite->device);
    
    // Should transition to lose
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_TRUE(resultState.transitionToLose());
}

// ============================================
// Two-Device Simulation Tests
// Simulates both hunter and bounty perspectives
// ============================================

class TwoDeviceSimulationTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        // Hunter setup
        hunter = new Player();
        char hunterId[] = "hunt";
        hunter->setUserID(hunterId);
        hunter->setIsHunter(true);
        hunter->setOpponentMacAddress("BB:BB:BB:BB:BB:BB");

        hunterMatchManager = new MatchManager();
        hunterWirelessManager = new QuickdrawWirelessManager();
        hunterWirelessManager->initialize(hunter, &hunterPeerComms, 100);
        hunterMatchManager->initialize(hunter, &hunterStorage, &hunterPeerComms, hunterWirelessManager);

        // Bounty setup
        bounty = new Player();
        char bountyId[] = "boun";
        bounty->setUserID(bountyId);
        bounty->setIsHunter(false);
        bounty->setOpponentMacAddress("AA:AA:AA:AA:AA:AA");

        bountyMatchManager = new MatchManager();
        bountyWirelessManager = new QuickdrawWirelessManager();
        bountyWirelessManager->initialize(bounty, &bountyPeerComms, 100);
        bountyMatchManager->initialize(bounty, &bountyStorage, &bountyPeerComms, bountyWirelessManager);

        // Create match on both sides
        hunterMatchManager->createMatch("two-device-match-id-123456", "hunt", "boun");
        bountyMatchManager->createMatch("two-device-match-id-123456", "hunt", "boun");
        
        hunterMatchManager->setDuelLocalStartTime(10000);
        bountyMatchManager->setDuelLocalStartTime(10000);

        ON_CALL(hunterPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(bountyPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        hunterMatchManager->clearCurrentMatch();
        bountyMatchManager->clearCurrentMatch();
        delete hunterMatchManager;
        delete bountyMatchManager;
        delete hunterWirelessManager;
        delete bountyWirelessManager;
        delete hunter;
        delete bounty;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Simulate hunter sending packet to bounty
    void hunterSendsToBounty(int command) {
        Match* match = hunterMatchManager->getCurrentMatch();
        TestQuickdrawPacket packet;
        strncpy(packet.matchId, match->getMatchId().c_str(), 36);
        packet.matchId[36] = '\0';
        strncpy(packet.hunterId, "hunt", 4);
        packet.hunterId[4] = '\0';
        strncpy(packet.bountyId, "boun", 4);
        packet.bountyId[4] = '\0';
        packet.hunterDrawTime = match->getHunterDrawTime();
        packet.bountyDrawTime = match->getBountyDrawTime();
        packet.command = command;
        
        uint8_t macAddr[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        bountyWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    // Simulate bounty sending packet to hunter
    void bountySendsToHunter(int command) {
        Match* match = bountyMatchManager->getCurrentMatch();
        TestQuickdrawPacket packet;
        strncpy(packet.matchId, match->getMatchId().c_str(), 36);
        packet.matchId[36] = '\0';
        strncpy(packet.hunterId, "hunt", 4);
        packet.hunterId[4] = '\0';
        strncpy(packet.bountyId, "boun", 4);
        packet.bountyId[4] = '\0';
        packet.hunterDrawTime = match->getHunterDrawTime();
        packet.bountyDrawTime = match->getBountyDrawTime();
        packet.command = command;
        
        uint8_t macAddr[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    Player* hunter;
    Player* bounty;
    MatchManager* hunterMatchManager;
    MatchManager* bountyMatchManager;
    QuickdrawWirelessManager* hunterWirelessManager;
    QuickdrawWirelessManager* bountyWirelessManager;
    MockPeerComms hunterPeerComms;
    MockPeerComms bountyPeerComms;
    MockStorage hunterStorage;
    MockStorage bountyStorage;
    FakePlatformClock* fakeClock;
};

// Test: Two devices - hunter presses first, both agree on winner
inline void twoDeviceHunterPressesFirstBothAgree(TwoDeviceSimulationTests* suite) {
    // Wire up callbacks
    suite->hunterWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->bountyMatchManager, std::placeholders::_1)
    );
    
    // Hunter presses at 150ms
    suite->fakeClock->advance(150);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    // Bounty presses at 280ms (130ms later)
    suite->fakeClock->advance(130);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    // Exchange results
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    // Verify both sides have results
    EXPECT_TRUE(suite->hunterMatchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->bountyMatchManager->matchResultsAreIn());
    
    // Verify both agree: hunter wins (150ms < 280ms)
    EXPECT_TRUE(suite->hunterMatchManager->didWin());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());
}

// Test: Two devices - bounty presses first, both agree on winner
inline void twoDeviceBountyPressesFirstBothAgree(TwoDeviceSimulationTests* suite) {
    suite->hunterWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->bountyMatchManager, std::placeholders::_1)
    );
    
    // Bounty presses at 120ms
    suite->fakeClock->advance(120);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    // Hunter presses at 350ms (230ms later)
    suite->fakeClock->advance(230);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    // Exchange results
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    // Verify both agree: bounty wins (120ms < 350ms)
    EXPECT_FALSE(suite->hunterMatchManager->didWin());
    EXPECT_TRUE(suite->bountyMatchManager->didWin());
}

// Test: Two devices - close race, correct winner determined
inline void twoDeviceCloseRaceCorrectWinner(TwoDeviceSimulationTests* suite) {
    suite->hunterWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->hunterMatchManager, std::placeholders::_1)
    );
    suite->bountyWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, suite->bountyMatchManager, std::placeholders::_1)
    );
    
    // Very close: Hunter at 200ms, Bounty at 201ms
    suite->fakeClock->advance(200);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    suite->fakeClock->advance(1);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    // Exchange
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    // Hunter wins by 1ms
    EXPECT_TRUE(suite->hunterMatchManager->didWin());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());
    
    // Verify exact times
    EXPECT_EQ(suite->hunterMatchManager->getCurrentMatch()->getHunterDrawTime(), 200);
    EXPECT_EQ(suite->hunterMatchManager->getCurrentMatch()->getBountyDrawTime(), 201);
}

// ============================================
// Handshake Integration Tests
// Tests the complete handshake flow with actual packet parsing
// ============================================

class HandshakeIntegrationTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        // Hunter setup
        hunter = new Player();
        char hunterId[] = "hunt";
        hunter->setUserID(hunterId);
        hunter->setIsHunter(true);
        hunter->setOpponentMacAddress("BB:BB:BB:BB:BB:BB");

        hunterMatchManager = new MatchManager();
        hunterWirelessManager = new QuickdrawWirelessManager();
        hunterWirelessManager->initialize(hunter, &hunterPeerComms, 100);
        hunterMatchManager->initialize(hunter, &hunterStorage, &hunterPeerComms, hunterWirelessManager);

        // Bounty setup
        bounty = new Player();
        char bountyId[] = "boun";
        bounty->setUserID(bountyId);
        bounty->setIsHunter(false);
        bounty->setOpponentMacAddress("AA:AA:AA:AA:AA:AA");

        bountyMatchManager = new MatchManager();
        bountyWirelessManager = new QuickdrawWirelessManager();
        bountyWirelessManager->initialize(bounty, &bountyPeerComms, 100);
        bountyMatchManager->initialize(bounty, &bountyStorage, &bountyPeerComms, bountyWirelessManager);

        // Default mock expectations
        ON_CALL(*hunterDevice.mockDisplay, invalidateScreen()).WillByDefault(Return(hunterDevice.mockDisplay));
        ON_CALL(*hunterDevice.mockDisplay, drawImage(_)).WillByDefault(Return(hunterDevice.mockDisplay));
        ON_CALL(*bountyDevice.mockDisplay, invalidateScreen()).WillByDefault(Return(bountyDevice.mockDisplay));
        ON_CALL(*bountyDevice.mockDisplay, drawImage(_)).WillByDefault(Return(bountyDevice.mockDisplay));
        ON_CALL(hunterPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(bountyPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        hunterMatchManager->clearCurrentMatch();
        bountyMatchManager->clearCurrentMatch();
        delete hunterMatchManager;
        delete bountyMatchManager;
        delete hunterWirelessManager;
        delete bountyWirelessManager;
        delete hunter;
        delete bounty;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Create a handshake packet (CONNECTION_CONFIRMED, HUNTER_RECEIVE_MATCH, or BOUNTY_FINAL_ACK)
    TestQuickdrawPacket createHandshakePacket(int command, const std::string& matchId, 
                                               const std::string& hunterId, const std::string& bountyId) {
        TestQuickdrawPacket packet;
        strncpy(packet.matchId, matchId.c_str(), 36);
        packet.matchId[36] = '\0';
        strncpy(packet.hunterId, hunterId.c_str(), 4);
        packet.hunterId[4] = '\0';
        strncpy(packet.bountyId, bountyId.c_str(), 4);
        packet.bountyId[4] = '\0';
        packet.hunterDrawTime = 0;
        packet.bountyDrawTime = 0;
        packet.command = command;
        return packet;
    }

    // Simulate bounty sending packet to hunter
    void bountySendsToHunter(int command, const std::string& matchId, 
                             const std::string& hunterId, const std::string& bountyId) {
        TestQuickdrawPacket packet = createHandshakePacket(command, matchId, hunterId, bountyId);
        uint8_t macAddr[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    // Simulate hunter sending packet to bounty
    void hunterSendsToBounty(int command, const std::string& matchId, 
                             const std::string& hunterId, const std::string& bountyId) {
        TestQuickdrawPacket packet = createHandshakePacket(command, matchId, hunterId, bountyId);
        uint8_t macAddr[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        bountyWirelessManager->processQuickdrawCommand(
            macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    Player* hunter;
    Player* bounty;
    MatchManager* hunterMatchManager;
    MatchManager* bountyMatchManager;
    QuickdrawWirelessManager* hunterWirelessManager;
    QuickdrawWirelessManager* bountyWirelessManager;
    MockDevice hunterDevice;
    MockDevice bountyDevice;
    MockPeerComms hunterPeerComms;
    MockPeerComms bountyPeerComms;
    MockStorage hunterStorage;
    MockStorage bountyStorage;
    FakePlatformClock* fakeClock;
};

// Test: Complete handshake flow from bounty's perspective
inline void handshakeCompleteBountyPerspective(HandshakeIntegrationTests* suite) {
    // Create BountySendCC state
    BountySendConnectionConfirmedState bountyState(suite->bounty, suite->bountyMatchManager, suite->bountyWirelessManager);
    bountyState.onStateMounted(&suite->bountyDevice);
    
    // Bounty should have sent CONNECTION_CONFIRMED (verified by mock)
    // Should not transition yet
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());
    
    // Simulate receiving HUNTER_RECEIVE_MATCH from hunter
    // Hunter would have created match with their ID included
    std::string matchId = "handshake-test-match-id-123456";
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH, matchId, "hunt", "boun");
    
    // Bounty should now have a match and transition
    EXPECT_TRUE(bountyState.transitionToConnectionSuccessful());
    EXPECT_NE(suite->bountyMatchManager->getCurrentMatch(), nullptr);
    EXPECT_EQ(suite->bountyMatchManager->getCurrentMatch()->getMatchId(), matchId);
    EXPECT_EQ(suite->bountyMatchManager->getCurrentMatch()->getHunterId(), "hunt");
    EXPECT_EQ(suite->bountyMatchManager->getCurrentMatch()->getBountyId(), "boun");
}

// Test: Complete handshake flow from hunter's perspective
inline void handshakeCompleteHunterPerspective(HandshakeIntegrationTests* suite) {
    // Create HunterSendId state
    HunterSendIdState hunterState(suite->hunter, suite->hunterMatchManager, suite->hunterWirelessManager);
    hunterState.onStateMounted(&suite->hunterDevice);
    
    // Should not transition yet
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
    
    // Simulate receiving CONNECTION_CONFIRMED from bounty
    std::string matchId = "handshake-test-match-id-123456";
    suite->bountySendsToHunter(QDCommand::CONNECTION_CONFIRMED, matchId, "", "boun");
    
    // Hunter should have created match, but not transitioned yet
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
    EXPECT_NE(suite->hunterMatchManager->getCurrentMatch(), nullptr);
    
    // Simulate receiving BOUNTY_FINAL_ACK
    suite->bountySendsToHunter(QDCommand::BOUNTY_FINAL_ACK, matchId, "hunt", "boun");
    
    // Now hunter should transition
    EXPECT_TRUE(hunterState.transitionToConnectionSuccessful());
}

// Test: Full two-device handshake - both devices end up with matching data
inline void handshakeTwoDeviceFullFlow(HandshakeIntegrationTests* suite) {
    // Create states for both devices
    HunterSendIdState hunterState(suite->hunter, suite->hunterMatchManager, suite->hunterWirelessManager);
    BountySendConnectionConfirmedState bountyState(suite->bounty, suite->bountyMatchManager, suite->bountyWirelessManager);
    
    // Mount both states
    hunterState.onStateMounted(&suite->hunterDevice);
    bountyState.onStateMounted(&suite->bountyDevice);
    
    // Neither should transition yet
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());
    
    // Step 1: Bounty sends CONNECTION_CONFIRMED (simulated on bounty mount)
    // Step 2: Hunter receives it and creates match
    std::string matchId = "two-device-handshake-match-12345";
    suite->bountySendsToHunter(QDCommand::CONNECTION_CONFIRMED, matchId, "", "boun");
    
    // Hunter has match now but not transitioned
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
    Match* hunterMatch = suite->hunterMatchManager->getCurrentMatch();
    ASSERT_NE(hunterMatch, nullptr);
    
    // Step 3: Hunter sends HUNTER_RECEIVE_MATCH to bounty
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH, 
                               hunterMatch->getMatchId(),
                               hunterMatch->getHunterId(),
                               hunterMatch->getBountyId());
    
    // Bounty should transition and have match
    EXPECT_TRUE(bountyState.transitionToConnectionSuccessful());
    Match* bountyMatch = suite->bountyMatchManager->getCurrentMatch();
    ASSERT_NE(bountyMatch, nullptr);
    
    // Step 4: Bounty sends BOUNTY_FINAL_ACK
    suite->bountySendsToHunter(QDCommand::BOUNTY_FINAL_ACK,
                               bountyMatch->getMatchId(),
                               bountyMatch->getHunterId(),
                               bountyMatch->getBountyId());
    
    // Hunter should now transition
    EXPECT_TRUE(hunterState.transitionToConnectionSuccessful());
    
    // Both devices should have matching match data
    EXPECT_EQ(hunterMatch->getMatchId(), bountyMatch->getMatchId());
    EXPECT_EQ(hunterMatch->getHunterId(), bountyMatch->getHunterId());
    EXPECT_EQ(hunterMatch->getBountyId(), bountyMatch->getBountyId());
}

// Test: Handshake timeout before completion
inline void handshakeTimeoutBeforeCompletion(HandshakeIntegrationTests* suite) {
    // First initialize the timeout through HandshakeInitiateState
    HandshakeInitiateState initiateState(suite->hunter);
    initiateState.onStateMounted(&suite->hunterDevice);
    initiateState.onStateLoop(&suite->hunterDevice);
    
    // Create HunterSendId state
    HunterSendIdState hunterState(suite->hunter, suite->hunterMatchManager, suite->hunterWirelessManager);
    hunterState.onStateMounted(&suite->hunterDevice);
    
    // Should not timeout initially
    EXPECT_FALSE(hunterState.transitionToIdle());
    
    // Advance past timeout (20 seconds + 1ms)
    suite->fakeClock->advance(20001);
    
    // Should timeout and transition to idle
    EXPECT_TRUE(hunterState.transitionToIdle());
    
    // Should NOT transition to connection successful
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
}

// Test: Handshake packet with invalid data is rejected
inline void handshakeRejectsInvalidPacketData(HandshakeIntegrationTests* suite) {
    HunterSendIdState hunterState(suite->hunter, suite->hunterMatchManager, suite->hunterWirelessManager);
    hunterState.onStateMounted(&suite->hunterDevice);
    
    // Send CONNECTION_CONFIRMED with empty match ID
    suite->bountySendsToHunter(QDCommand::CONNECTION_CONFIRMED, "", "", "boun");
    
    // Hunter should not have created a match (validation fails)
    EXPECT_EQ(suite->hunterMatchManager->getCurrentMatch(), nullptr);
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
}

// Test: Handshake ignores unexpected commands
inline void handshakeIgnoresUnexpectedCommands(HandshakeIntegrationTests* suite) {
    HunterSendIdState hunterState(suite->hunter, suite->hunterMatchManager, suite->hunterWirelessManager);
    hunterState.onStateMounted(&suite->hunterDevice);
    
    // Send DRAW_RESULT instead of expected handshake command
    TestQuickdrawPacket packet = suite->createHandshakePacket(QDCommand::DRAW_RESULT, "test-match", "hunt", "boun");
    uint8_t macAddr[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    suite->hunterWirelessManager->processQuickdrawCommand(
        macAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
    );
    
    // Should not create match or transition
    EXPECT_EQ(suite->hunterMatchManager->getCurrentMatch(), nullptr);
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
}

// Test: Handshake sets opponent MAC address correctly
inline void handshakeSetsOpponentMacAddress(HandshakeIntegrationTests* suite) {
    // Clear hunter's opponent MAC to test it gets set
    suite->hunter->setOpponentMacAddress("");
    
    HunterSendIdState hunterState(suite->hunter, suite->hunterMatchManager, suite->hunterWirelessManager);
    hunterState.onStateMounted(&suite->hunterDevice);
    
    // Send CONNECTION_CONFIRMED - the MAC comes from the packet sender
    std::string matchId = "mac-test-match-id-1234567890";
    suite->bountySendsToHunter(QDCommand::CONNECTION_CONFIRMED, matchId, "", "boun");
    
    // Hunter should have set opponent MAC from the packet
    ASSERT_NE(suite->hunter->getOpponentMacAddress(), nullptr);
    EXPECT_EQ(*suite->hunter->getOpponentMacAddress(), "BB:BB:BB:BB:BB:BB");
}

// Test: Match data is correctly propagated through handshake
inline void handshakeMatchDataPropagatedCorrectly(HandshakeIntegrationTests* suite) {
    BountySendConnectionConfirmedState bountyState(suite->bounty, suite->bountyMatchManager, suite->bountyWirelessManager);
    bountyState.onStateMounted(&suite->bountyDevice);
    
    // Simulate receiving HUNTER_RECEIVE_MATCH with specific data
    std::string matchId = "propagation-test-match-id-12345";
    std::string hunterId = "hunt";
    std::string bountyId = "boun";
    
    suite->hunterSendsToBounty(QDCommand::HUNTER_RECEIVE_MATCH, matchId, hunterId, bountyId);
    
    // Verify bounty's match has all the correct data
    Match* match = suite->bountyMatchManager->getCurrentMatch();
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->getMatchId(), matchId);
    EXPECT_EQ(match->getHunterId(), hunterId);
    EXPECT_EQ(match->getBountyId(), bountyId);
    EXPECT_EQ(match->getHunterDrawTime(), 0);
    EXPECT_EQ(match->getBountyDrawTime(), 0);
}
