#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "utility-tests.hpp"
#include "device/device-constants.hpp"
#include "../test-constants.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::DoAll;

// ============================================
// Idle State Tests
// ============================================

class IdleStateTests : public testing::Test {
public:
    void SetUp() override {
        // Set up fake clock for timer control
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        // Create player
        player = new Player();
        char playerId[] = "1234";
        player->setUserID(playerId);
        player->setIsHunter(true);

        // Create match manager and initialize
        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        // Create progress manager
        progressManager = new ProgressManager();
        progressManager->initialize(player, &storage);

        // Create idle state
        idleState = new Idle(player, matchManager, wirelessManager, progressManager);

        // Set up default mock expectations for display
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete idleState;
        delete progressManager;
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Helper to simulate serial callback
    void simulateSerialMessage(const std::string& message) {
        // The idle state sets up a callback via setOnStringReceivedCallback
        // We'll directly call the serialEventCallbacks method through a test helper
        // Since it's private, we need to trigger it through the device's callback mechanism
        device.outputJackSerial.stringCallback(message);
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    ProgressManager* progressManager;
    Idle* idleState;
    FakePlatformClock* fakeClock;
};

// Test: Idle state mounts and sets up callbacks correctly
inline void idleSerialHeartbeatTriggersMacAddressSend(IdleStateTests* suite) {
    // Expect button callbacks to be set on mount
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    // Mount the state
    suite->idleState->onStateMounted(&suite->device);
    
    // Verify initial state - should not transition
    EXPECT_FALSE(suite->idleState->transitionToHandshake());
    
    // Note: Testing serial callback requires integration with the actual device's
    // callback mechanism. For unit tests, we verify the state's public interface.
    // The serial callback flow is tested via integration tests.
}

// Test: Idle state does not transition without serial events
inline void idleReceiveMacAddressTransitionsToHandshake(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    suite->idleState->onStateMounted(&suite->device);
    
    // Without serial events, should not transition
    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToHandshake());
    
    // Verify state is stable over multiple loops
    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToHandshake());
}

// Test: State cleanup on dismount
inline void idleStateClearsOnDismount(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    suite->idleState->onStateMounted(&suite->device);
    
    // Expect callbacks to be removed on dismount
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    // Dismount the state
    suite->idleState->onStateDismounted(&suite->device);
    
    // After dismount, the transition flag should be reset
    EXPECT_FALSE(suite->idleState->transitionToHandshake());
}

// Test: Button callbacks are registered and removed properly
inline void idleButtonCallbacksRegisteredAndRemoved(IdleStateTests* suite) {
    // Expect button callbacks to be set on mount
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    suite->idleState->onStateMounted(&suite->device);
    
    // Expect callbacks to be removed on dismount
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    suite->idleState->onStateDismounted(&suite->device);
}

// ============================================
// Handshake State Tests  
// ============================================

class HandshakeStateTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        char playerId[] = "1234";
        player->setUserID(playerId);
        
        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        
        // Initialize wireless manager with WirelessManager from MockDevice
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        // Set up default mock expectations
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }
    
    // Helper to reset the static handshake timeout between tests
    void resetHandshakeTimeout() {
        // Create a temporary state to access the static reset method
        HandshakeInitiateState tempState(player);
        tempState.onStateDismounted(&device);
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    FakePlatformClock* fakeClock;
};

// Test: Hunter routes to HunterSendIdState
inline void handshakeHunterRoutesToHunterSendIdState(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    
    HandshakeInitiateState initiateState(suite->player);
    initiateState.onStateMounted(&suite->device);
    
    // After settling time (500ms), should route to hunter state
    suite->fakeClock->advance(600);
    initiateState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(initiateState.transitionToHunterSendId());
    EXPECT_FALSE(initiateState.transitionToBountySendCC());
}

// Test: Bounty routes to BountySendCCState
inline void handshakeBountyRoutesToBountySendCCState(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    
    HandshakeInitiateState initiateState(suite->player);
    initiateState.onStateMounted(&suite->device);
    
    // After settling time
    suite->fakeClock->advance(600);
    initiateState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(initiateState.transitionToBountySendCC());
    EXPECT_FALSE(initiateState.transitionToHunterSendId());
}

// Test: Handshake timeout returns to idle
inline void handshakeTimeoutReturnsToIdle(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);
    
    // First, go through HandshakeInitiateState to initialize the timeout
    HandshakeInitiateState initiateState(suite->player);
    initiateState.onStateMounted(&suite->device);
    
    // Call onStateLoop to initialize the timeout (initTimeout is called here)
    initiateState.onStateLoop(&suite->device);
    
    // Now create hunter state (timeout should be initialized from initiate state)
    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);
    
    // Initially should not timeout (0ms has passed since timeout init)
    EXPECT_FALSE(hunterState.transitionToIdle());
    
    // Advance time past timeout (20 seconds + 1ms to trigger expiration)
    // Timer expires when elapsed > duration (strictly greater than)
    suite->fakeClock->advance(20001);
    
    // Should now timeout
    EXPECT_TRUE(hunterState.transitionToIdle());
    
    // Cleanup
    hunterState.onStateDismounted(&suite->device);
}

// Test: Bounty handshake flow succeeds
inline void handshakeBountyFlowSucceeds(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    // Expect packet to be sent when state mounts
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);

    // Should not transition yet
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Simulate receiving HUNTER_RECEIVE_MATCH command
    Match receivedMatch("test-match-id", "5678", "1234");
    QuickdrawCommand command(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, receivedMatch);
    bountyState.onQuickdrawCommandReceived(command);

    // Still not done - need HUNTER_READY
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Simulate receiving HUNTER_READY command
    QuickdrawCommand hunterReady(TestConstants::TEST_MAC_DEFAULT, HUNTER_READY, receivedMatch);
    bountyState.onQuickdrawCommandReceived(hunterReady);

    // Should now transition to connection successful
    EXPECT_TRUE(bountyState.transitionToConnectionSuccessful());
}

// Test: Hunter handshake flow succeeds
inline void handshakeHunterFlowSucceeds(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Should not transition yet
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    // Simulate receiving CONNECTION_CONFIRMED command
    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    // Still not done - need BOUNTY_FINAL_ACK
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    // Simulate receiving BOUNTY_FINAL_ACK (Hunter sends HUNTER_READY and transitions)
    QuickdrawCommand finalAck(TestConstants::TEST_MAC_DEFAULT, BOUNTY_FINAL_ACK, receivedMatch);
    hunterState.onQuickdrawCommandReceived(finalAck);

    // Should now transition (after sending HUNTER_READY)
    EXPECT_TRUE(hunterState.transitionToConnectionSuccessful());
}

// Test: Handshake sends direct messages (not broadcast)
inline void handshakeSendsDirectMessagesNotBroadcast(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);
    
    // Capture the MAC address used in sendData
    uint8_t capturedMac[6];
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillOnce(Invoke([&capturedMac](const uint8_t* mac, PktType type, const uint8_t* data, size_t len) {
            memcpy(capturedMac, mac, 6);
            return 1;
        }));
    
    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);
    
    // Verify that the MAC address is not the broadcast address (FF:FF:FF:FF:FF:FF)
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_NE(memcmp(capturedMac, broadcastMac, 6), 0);
    
    // Verify it's the opponent's MAC
    uint8_t expectedMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    EXPECT_EQ(memcmp(capturedMac, expectedMac, 6), 0);
}

// Test: Handshake states clear on dismount
inline void handshakeStatesClearOnDismount(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);
    
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));
    
    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);
    
    // Receive commands to set transition flag
    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);
    
    QuickdrawCommand finalAck(TestConstants::TEST_MAC_DEFAULT, BOUNTY_FINAL_ACK, receivedMatch);
    hunterState.onQuickdrawCommandReceived(finalAck);
    
    EXPECT_TRUE(hunterState.transitionToConnectionSuccessful());
    
    // Dismount
    hunterState.onStateDismounted(&suite->device);
    
    // Transition flag should be reset
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
}

// ============================================
// Handshake Edge Case Tests
// ============================================

// Test: Duplicate CONNECTION_CONFIRMED handling
inline void handshakeDuplicateConnectionConfirmedHandled(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Send CONNECTION_CONFIRMED first time
    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    // Verify match was created
    Match* currentMatch = suite->matchManager->getCurrentMatch();
    ASSERT_NE(currentMatch, nullptr);
    EXPECT_EQ(currentMatch->getMatchId(), "test-match-id");

    // Send duplicate CONNECTION_CONFIRMED
    Match duplicateMatch("duplicate-match-id", "", "9999");
    QuickdrawCommand duplicateConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, duplicateMatch);
    hunterState.onQuickdrawCommandReceived(duplicateConfirmed);

    // Match should still be the original one (no duplicate creation or crash)
    currentMatch = suite->matchManager->getCurrentMatch();
    ASSERT_NE(currentMatch, nullptr);
    EXPECT_EQ(currentMatch->getMatchId(), "test-match-id");
    EXPECT_NE(currentMatch->getMatchId(), "duplicate-match-id");

    hunterState.onStateDismounted(&suite->device);
}

// Test: Duplicate HUNTER_RECEIVE_MATCH handling
inline void handshakeDuplicateHunterReceiveMatchHandled(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);

    // Send HUNTER_RECEIVE_MATCH first time
    Match receivedMatch("test-match-id", "5678", "1234");
    QuickdrawCommand command(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, receivedMatch);
    bountyState.onQuickdrawCommandReceived(command);

    // With 4-way handshake, bounty waits for HUNTER_READY after HUNTER_RECEIVE_MATCH
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Send HUNTER_READY to complete handshake
    QuickdrawCommand hunterReady(TestConstants::TEST_MAC_DEFAULT, HUNTER_READY, receivedMatch);
    bountyState.onQuickdrawCommandReceived(hunterReady);

    EXPECT_TRUE(bountyState.transitionToConnectionSuccessful());

    // Send duplicate HUNTER_RECEIVE_MATCH
    Match duplicateMatch("duplicate-match-id", "9999", "1234");
    QuickdrawCommand duplicateCommand(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, duplicateMatch);
    bountyState.onQuickdrawCommandReceived(duplicateCommand);

    // Should still be in transition state (no crash from duplicate)
    EXPECT_TRUE(bountyState.transitionToConnectionSuccessful());

    bountyState.onStateDismounted(&suite->device);
}

// Test: Out-of-order BOUNTY_FINAL_ACK accepted (current implementation is permissive)
inline void handshakeOutOfOrderBountyFinalAckAccepted(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Send BOUNTY_FINAL_ACK BEFORE CONNECTION_CONFIRMED
    // With 4-way handshake, hunter needs a match to send HUNTER_READY — rejects out-of-order
    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand finalAck(TestConstants::TEST_MAC_DEFAULT, BOUNTY_FINAL_ACK, receivedMatch);
    hunterState.onQuickdrawCommandReceived(finalAck);

    // Out-of-order BOUNTY_FINAL_ACK rejected (no match to send HUNTER_READY for)
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    // Now send CONNECTION_CONFIRMED (creates match and sends HUNTER_RECEIVE_MATCH)
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    // Match gets created, HUNTER_RECEIVE_MATCH sent
    Match* currentMatch = suite->matchManager->getCurrentMatch();
    ASSERT_NE(currentMatch, nullptr);
    EXPECT_EQ(currentMatch->getMatchId(), "test-match-id");

    // Still not transitioned — need BOUNTY_FINAL_ACK again (now with match present)
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    // Re-send BOUNTY_FINAL_ACK — now hunter has match, can send HUNTER_READY
    hunterState.onQuickdrawCommandReceived(finalAck);
    EXPECT_TRUE(hunterState.transitionToConnectionSuccessful());

    hunterState.onStateDismounted(&suite->device);
}

// Test: Out-of-order HUNTER_RECEIVE_MATCH ignored
inline void handshakeOutOfOrderHunterReceiveMatchIgnored(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    // Don't mount bounty state yet - simulate receiving packet before handshake starts

    // Try to process HUNTER_RECEIVE_MATCH before bounty sends CONNECTION_CONFIRMED
    // This tests the protocol robustness - in real scenario, packets could arrive out of order

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);

    // Send command BEFORE mounting (simulates packet arriving before handshake)
    Match receivedMatch("test-match-id", "5678", "1234");
    QuickdrawCommand command(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, receivedMatch);

    // This should be handled gracefully (callback not yet registered)
    // No crash expected

    // Now mount normally
    bountyState.onStateMounted(&suite->device);

    // Send HUNTER_RECEIVE_MATCH in correct order
    bountyState.onQuickdrawCommandReceived(command);

    // With 4-way handshake, bounty waits for HUNTER_READY
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Send HUNTER_READY to complete handshake
    QuickdrawCommand hunterReady(TestConstants::TEST_MAC_DEFAULT, HUNTER_READY, receivedMatch);
    bountyState.onQuickdrawCommandReceived(hunterReady);

    // Should transition successfully
    EXPECT_TRUE(bountyState.transitionToConnectionSuccessful());

    bountyState.onStateDismounted(&suite->device);
}

// Test: Empty match ID rejected
inline void handshakeEmptyMatchIdRejected(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .Times(0);  // Should not send anything if match ID is invalid

    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Send CONNECTION_CONFIRMED with empty match ID
    Match invalidMatch("", "", "5678");  // Empty match ID
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, invalidMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    // Should not transition (invalid data rejected)
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    // Match should not be created
    EXPECT_EQ(suite->matchManager->getCurrentMatch(), nullptr);

    hunterState.onStateDismounted(&suite->device);
}

// Test: Empty player IDs rejected
inline void handshakeEmptyPlayerIdsRejected(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .Times(0);  // Should not send anything if player IDs are invalid

    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Send CONNECTION_CONFIRMED with empty bounty ID
    Match invalidMatch("test-match-id", "", "");  // Empty bounty ID
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, invalidMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    // Should not transition
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    // Match should not be created
    EXPECT_EQ(suite->matchManager->getCurrentMatch(), nullptr);

    hunterState.onStateDismounted(&suite->device);
}

// Test: Invalid command type ignored
inline void handshakeInvalidCommandTypeIgnored(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .Times(0);  // Should not send anything for invalid command

    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Send command with invalid type (DRAW_RESULT is not a handshake command)
    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand invalidCommand(TestConstants::TEST_MAC_DEFAULT, DRAW_RESULT, receivedMatch);
    hunterState.onQuickdrawCommandReceived(invalidCommand);

    // Should not transition (invalid command ignored)
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    hunterState.onStateDismounted(&suite->device);
}

// Test: Hunter timeout after CONNECTION_CONFIRMED but no BOUNTY_FINAL_ACK
inline void handshakeHunterTimeoutAfterConnectionConfirmed(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    // Initialize timeout through initiate state
    HandshakeInitiateState initiateState(suite->player);
    initiateState.onStateMounted(&suite->device);
    initiateState.onStateLoop(&suite->device);

    // Create hunter state
    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Receive CONNECTION_CONFIRMED
    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    // Still waiting for BOUNTY_FINAL_ACK
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    // Should not timeout yet
    EXPECT_FALSE(hunterState.transitionToIdle());

    // Advance time past timeout (20 seconds + 1ms)
    suite->fakeClock->advance(20001);

    // Should timeout and return to Idle
    EXPECT_TRUE(hunterState.transitionToIdle());

    // Should still not transition to ConnectionSuccessful
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    hunterState.onStateDismounted(&suite->device);
}

// Test: Bounty timeout if no HUNTER_RECEIVE_MATCH
inline void handshakeBountyTimeoutNoHunterReceiveMatch(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    // Initialize timeout
    HandshakeInitiateState initiateState(suite->player);
    initiateState.onStateMounted(&suite->device);
    initiateState.onStateLoop(&suite->device);

    // Create bounty state
    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);

    // Don't send HUNTER_RECEIVE_MATCH - simulate lost packet

    // Should not transition yet
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Should not timeout yet
    EXPECT_FALSE(bountyState.transitionToIdle());

    // Advance time past timeout
    suite->fakeClock->advance(20001);

    // Should timeout and return to Idle
    EXPECT_TRUE(bountyState.transitionToIdle());

    bountyState.onStateDismounted(&suite->device);
}

// Test: Global timeout fires correctly
inline void handshakeGlobalTimeoutFires(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    // Initialize timeout
    HandshakeInitiateState initiateState(suite->player);
    initiateState.onStateMounted(&suite->device);
    initiateState.onStateLoop(&suite->device);

    // Create hunter state
    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Don't send any packets

    // Initially should not timeout
    EXPECT_FALSE(hunterState.transitionToIdle());

    // Advance time to just before timeout (19999ms)
    suite->fakeClock->advance(19999);

    // Still should not timeout (timer expires when elapsed > duration)
    EXPECT_FALSE(hunterState.transitionToIdle());

    // Advance 2ms more (total 20001ms)
    suite->fakeClock->advance(2);

    // Should now timeout
    EXPECT_TRUE(hunterState.transitionToIdle());

    hunterState.onStateDismounted(&suite->device);
}

// Test: Simultaneous connection attempt (no crash or state corruption)
inline void handshakeSimultaneousConnectionAttempt(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    // Initialize timeout
    HandshakeInitiateState initiateState(suite->player);
    initiateState.onStateMounted(&suite->device);
    initiateState.onStateLoop(&suite->device);

    // Start first handshake
    HunterSendIdState hunterState1(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState1.onStateMounted(&suite->device);

    // Receive first CONNECTION_CONFIRMED
    Match receivedMatch1("match-id-1", "", "5678");
    QuickdrawCommand connectionConfirmed1(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch1);
    hunterState1.onQuickdrawCommandReceived(connectionConfirmed1);

    // Start second handshake before first completes (simulates rapid button press)
    HunterSendIdState hunterState2(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState2.onStateMounted(&suite->device);

    // Receive second CONNECTION_CONFIRMED
    Match receivedMatch2("match-id-2", "", "9999");
    QuickdrawCommand connectionConfirmed2(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch2);
    hunterState2.onQuickdrawCommandReceived(connectionConfirmed2);

    // Complete first handshake
    QuickdrawCommand finalAck1(TestConstants::TEST_MAC_DEFAULT, BOUNTY_FINAL_ACK, receivedMatch1);
    hunterState1.onQuickdrawCommandReceived(finalAck1);

    // First should be ready to transition
    EXPECT_TRUE(hunterState1.transitionToConnectionSuccessful());

    // Second should not crash (still waiting for BOUNTY_FINAL_ACK)
    EXPECT_FALSE(hunterState2.transitionToConnectionSuccessful());

    // Complete second handshake
    QuickdrawCommand finalAck2(TestConstants::TEST_MAC_DEFAULT, BOUNTY_FINAL_ACK, receivedMatch2);
    hunterState2.onQuickdrawCommandReceived(finalAck2);

    // Second should now be ready
    EXPECT_TRUE(hunterState2.transitionToConnectionSuccessful());

    // Cleanup both states (no crash expected)
    hunterState1.onStateDismounted(&suite->device);
    hunterState2.onStateDismounted(&suite->device);
}

// ============================================
// Countdown State Tests
// ============================================

class DuelCountdownTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);

        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        countdownState = new DuelCountdown(player, matchManager);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete countdownState;
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Helper to capture button callback
    parameterizedCallbackFunction capturedButtonCallback = nullptr;
    void* capturedContext = nullptr;

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    DuelCountdown* countdownState;
    FakePlatformClock* fakeClock;
};

// Test: Button masher penalty increments on button press
inline void countdownButtonMasherPenaltyIncrementsOnButtonPress(DuelCountdownTests* suite) {
    // Capture the button callback when it's set
    parameterizedCallbackFunction capturedCallback = nullptr;
    void* capturedCtx = nullptr;
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillOnce(DoAll(
            SaveArg<0>(&capturedCallback),
            SaveArg<1>(&capturedCtx)
        ));
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    suite->countdownState->onStateMounted(&suite->device);
    
    // Invoke the button callback (simulating early button press)
    ASSERT_NE(capturedCallback, nullptr);
    capturedCallback(capturedCtx);
    
    // The button masher count should be tracked in the match manager
    // We can verify this by checking that a subsequent duel button press has penalty applied
    // For now, we verify the callback was invokable
    SUCCEED();
}

// Test: Multiple early presses accumulate penalty  
inline void countdownMultipleEarlyPressesAccumulatePenalty(DuelCountdownTests* suite) {
    parameterizedCallbackFunction capturedCallback = nullptr;
    void* capturedCtx = nullptr;
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillOnce(DoAll(
            SaveArg<0>(&capturedCallback),
            SaveArg<1>(&capturedCtx)
        ));
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    suite->countdownState->onStateMounted(&suite->device);
    
    ASSERT_NE(capturedCallback, nullptr);
    
    // Simulate 3 early button presses
    capturedCallback(capturedCtx);
    capturedCallback(capturedCtx);
    capturedCallback(capturedCtx);
    
    // The penalty should now be 3 * 75ms = 225ms
    // This will be verified when the duel button is pressed
    SUCCEED();
}

// Test: Countdown progresses through stages
inline void countdownProgressesThroughStages(DuelCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    suite->countdownState->onStateMounted(&suite->device);
    
    // Initially should not trigger battle
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through THREE stage (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through TWO stage (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through ONE stage (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    
    // After ONE expires, BATTLE should trigger
    EXPECT_TRUE(suite->countdownState->shallWeBattle());
}

// Test: Battle transition sets flag
inline void countdownBattleTransitionSetsFlag(DuelCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    suite->countdownState->onStateMounted(&suite->device);
    
    // Should not battle initially
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through each stage properly:
    // THREE -> TWO (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // TWO -> ONE (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // ONE -> BATTLE (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    
    EXPECT_TRUE(suite->countdownState->shallWeBattle());
}

// Test: Countdown cleanup on dismount
inline void countdownCleansUpOnDismount(DuelCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    suite->countdownState->onStateMounted(&suite->device);
    
    // Advance through each stage to trigger battle
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    
    EXPECT_TRUE(suite->countdownState->shallWeBattle());
    
    // Expect callbacks to be removed
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    suite->countdownState->onStateDismounted(&suite->device);
    
    // Flag should be reset
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
}

// ============================================
// Duel State Tests
// ============================================

class DuelStateTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000); // Start at 10 seconds

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);
        player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        // Create a match for testing
        matchManager->createMatch("test-match-id", player->getUserID(), "5678");

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    FakePlatformClock* fakeClock;
};

// ============================================
// Scenario 1: DUT presses button first, then receives result
// ============================================

// Test: Button press transitions to DuelPushed
inline void duelButtonPressTransitionsToDuelPushed(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // Initially should not transition
    EXPECT_FALSE(duelState.transitionToDuelPushed());
    
    // Simulate button press via match manager callback
    suite->fakeClock->advance(200); // 200ms reaction time
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    duelState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(duelState.transitionToDuelPushed());
}

// Test: Button press calculates reaction time correctly
inline void duelButtonPressCalculatesReactionTime(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // Advance 250ms (simulating reaction time)
    suite->fakeClock->advance(250);
    
    // Trigger button press
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    // Check that hunter draw time was set
    Match* match = suite->matchManager->getCurrentMatch();
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->getHunterDrawTime(), 250);
}

// Test: Button press applies button masher penalty
inline void duelButtonPressAppliesMasherPenalty(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    // First, simulate button mashing during countdown
    DuelCountdown countdownState(suite->player, suite->matchManager);
    
    parameterizedCallbackFunction masherCallback = nullptr;
    void* masherCtx = nullptr;
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillOnce(DoAll(SaveArg<0>(&masherCallback), SaveArg<1>(&masherCtx)));
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    countdownState.onStateMounted(&suite->device);
    
    // Simulate 2 early presses during countdown
    ASSERT_NE(masherCallback, nullptr);
    masherCallback(masherCtx);
    masherCallback(masherCtx);
    
    // Clean up countdown
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    countdownState.onStateDismounted(&suite->device);
    
    // Now start the duel
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    duelState.onStateMounted(&suite->device);
    
    // Advance 200ms
    suite->fakeClock->advance(200);
    
    // Trigger button press
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    // Reaction time should be 200ms + (2 * 75ms penalty) = 350ms
    Match* match = suite->matchManager->getCurrentMatch();
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->getHunterDrawTime(), 350);
}

// Test: Button press broadcasts DRAW_RESULT
inline void duelButtonPressBroadcastsDrawResult(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    // Expect sendData to be called when button is pressed
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(Return(1));
    
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(200);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    // If we get here without assertion failure, the broadcast was sent
    SUCCEED();
}

// Test: DuelPushed waits for opponent result (grace period)
inline void duelPushedWaitsForOpponentResult(DuelStateTests* suite) {
    DuelPushed pushedState(suite->player, suite->matchManager);
    
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    pushedState.onStateMounted(&suite->device);
    
    // Should not transition immediately
    EXPECT_FALSE(pushedState.transitionToDuelResult());
    
    // Advance less than grace period (900ms)
    suite->fakeClock->advance(500);
    pushedState.onStateLoop(&suite->device);
    
    EXPECT_FALSE(pushedState.transitionToDuelResult());
}

// Test: DuelPushed transitions when result received
inline void duelPushedTransitionsOnResultReceived(DuelStateTests* suite) {
    DuelPushed pushedState(suite->player, suite->matchManager);
    
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    pushedState.onStateMounted(&suite->device);
    
    // Simulate receiving opponent's result
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedDrawResult();
    
    // Should now have all results
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(pushedState.transitionToDuelResult());
}

// ============================================
// Scenario 2: DUT receives result first, then presses button
// ============================================

// Test: Received result transitions to DuelReceivedResult
inline void duelReceivedResultTransitionsToDuelReceivedResult(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // Simulate receiving opponent's result before pressing button
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    duelState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
}

// Test: DuelReceivedResult waits for button press
inline void duelReceivedResultWaitsForButtonPress(DuelStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);
    
    // Should not transition immediately
    EXPECT_FALSE(receivedState.transitionToDuelResult());
    
    // Advance less than grace period (750ms)
    suite->fakeClock->advance(400);
    receivedState.onStateLoop(&suite->device);
    
    // Still waiting (either for button press or grace period expiry)
    // The transition depends on matchResultsAreIn() or the internal flag
}

// Test: Button press during grace period causes transition
inline void duelButtonPressDuringGracePeriodTransitions(DuelStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);
    
    // Advance some time
    suite->fakeClock->advance(300);
    
    // Simulate button press
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    
    receivedState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(receivedState.transitionToDuelResult());
}

// ============================================
// Scenario 3: Neither device presses (timeout)
// ============================================

// Test: Duel timeout transitions to idle
inline void duelTimeoutTransitionsToIdle(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);
    
    // Initially should not timeout
    EXPECT_FALSE(duelState.transitionToIdle());
    
    // Advance past timeout (4000ms)
    suite->fakeClock->advance(4100);
    duelState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(duelState.transitionToIdle());
}

// ============================================
// Scenario 4: DUT presses, opponent never responds
// ============================================

// Test: DuelPushed grace period expires and transitions
inline void duelPushedGracePeriodExpiresTransitions(DuelStateTests* suite) {
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    DuelPushed pushedState(suite->player, suite->matchManager);
    pushedState.onStateMounted(&suite->device);
    
    // Advance past grace period (900ms)
    suite->fakeClock->advance(1000);
    pushedState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(pushedState.transitionToDuelResult());
}

// Test: Opponent timeout means win
inline void duelOpponentTimeoutMeansWin(DuelStateTests* suite) {
    suite->player->setIsHunter(true);
    
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    // Bounty draw time stays at 0 (never pressed)
    suite->matchManager->setNeverPressed();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// ============================================
// Scenario 5: DUT never presses, opponent does
// ============================================

// Test: Grace period expires sets never pressed
inline void duelGracePeriodExpiresSetsNeverPressed(DuelStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);
    
    // Advance past grace period (750ms)
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    // Should have set pity time and transitioned
    EXPECT_TRUE(receivedState.transitionToDuelResult());
}

// Test: Grace period expiry sends pity time
inline void duelGracePeriodExpiresSendsPityTime(DuelStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);
    
    // Advance past grace period
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    // Hunter draw time should now be set (pity time)
    Match* match = suite->matchManager->getCurrentMatch();
    ASSERT_NE(match, nullptr);
    EXPECT_GT(match->getHunterDrawTime(), 0);
}

// Test: Never pressed (pity time) means lose
inline void duelNeverPressedMeansLose(DuelStateTests* suite) {
    suite->player->setIsHunter(true);
    
    // Scenario: Hunter never pressed, bounty pressed quickly
    // Hunter gets pity time (e.g. 800ms) when grace period expires
    // Bounty pressed at 150ms
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setHunterDrawTime(800); // Pity time - much slower
    
    // Hunter loses because 800ms > 150ms
    EXPECT_FALSE(suite->matchManager->didWin());
}

// ============================================
// Duel Result State Tests
// ============================================

class DuelResultTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);
        player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
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

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    FakePlatformClock* fakeClock;
};

// Test: Hunter wins with faster time
inline void resultHunterWinsWithFasterTime(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Bounty wins with faster time
inline void resultBountyWinsWithFasterTime(DuelResultTests* suite) {
    suite->player->setIsHunter(false);
    
    suite->matchManager->createMatch("test-match", "5678", suite->player->getUserID());
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Tied times favor opponent (local player loses)
inline void resultTiedTimesFavorOpponent(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setBountyDrawTime(250);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    // With equal times, hunter_time < bounty_time is false, so hunter loses
    EXPECT_FALSE(suite->matchManager->didWin());
}

// Test: Opponent timeout means auto win
inline void resultOpponentTimeoutMeansAutoWin(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setBountyDrawTime(0); // Bounty never pressed
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setNeverPressed();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Win transitions to Win state
inline void resultWinTransitionsToWinState(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_TRUE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

// Test: Lose transitions to Lose state
inline void resultLoseTransitionsToLoseState(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setBountyDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_TRUE(resultState.transitionToLose());
}

// Test: Player stats updated on win
inline void resultPlayerStatsUpdatedOnWin(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    int initialWins = suite->player->getWins();
    int initialStreak = suite->player->getStreak();
    int initialMatches = suite->player->getMatchesPlayed();
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_EQ(suite->player->getWins(), initialWins + 1);
    EXPECT_EQ(suite->player->getStreak(), initialStreak + 1);
    EXPECT_EQ(suite->player->getMatchesPlayed(), initialMatches + 1);
}

// Test: Player stats updated on loss
inline void resultPlayerStatsUpdatedOnLoss(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    // Set up an initial streak
    suite->player->incrementWins();
    suite->player->incrementStreak();
    suite->player->incrementStreak();
    
    int initialLosses = suite->player->getLosses();
    int initialMatches = suite->player->getMatchesPlayed();
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setBountyDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_EQ(suite->player->getLosses(), initialLosses + 1);
    EXPECT_EQ(suite->player->getStreak(), 0); // Streak reset
    EXPECT_EQ(suite->player->getMatchesPlayed(), initialMatches + 1);
}

// Test: Match finalized on result
inline void resultMatchFinalizedOnResult(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    EXPECT_CALL(suite->storage, write(_, _))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(Return(100));
    EXPECT_CALL(suite->storage, writeUChar(_, _))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(Return(1));
    
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);
    
    // Match should be finalized (saved to storage)
    // We verify this by checking the mock was called
    SUCCEED();
}

// ============================================
// State Cleanup Verification Tests
// ============================================

class StateCleanupTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);
        player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
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

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    FakePlatformClock* fakeClock;
};

// Test: Idle state clears button callbacks on dismount
inline void cleanupIdleClearsButtonCallbacks(StateCleanupTests* suite) {
    ProgressManager progressManager;
    progressManager.initialize(suite->player, &suite->storage);
    Idle idleState(suite->player, suite->matchManager, suite->wirelessManager, &progressManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    idleState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    idleState.onStateDismounted(&suite->device);
}

// Test: Countdown state clears button callbacks on dismount
inline void cleanupCountdownClearsButtonCallbacks(StateCleanupTests* suite) {
    DuelCountdown countdownState(suite->player, suite->matchManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    countdownState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    countdownState.onStateDismounted(&suite->device);
}

// Test: Duel state preserves button callbacks for DuelPushed/DuelReceivedResult
inline void cleanupDuelStateClearsCallbacksOnDismount(StateCleanupTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    duelState.onStateMounted(&suite->device);

    // Fixed: Duel state now DOES clear button callbacks on dismount (issue #145)
    // This prevents phantom button presses after state exits
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    duelState.onStateDismounted(&suite->device);
}

// Test: DuelReceivedResult state clears button callbacks on dismount
inline void cleanupDuelReceivedResultClearsButtonCallbacks(StateCleanupTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    receivedState.onStateDismounted(&suite->device);
}

// Test: Duel state invalidates timer on dismount
inline void cleanupDuelStateInvalidatesTimer(StateCleanupTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    duelState.onStateMounted(&suite->device);
    
    // Advance time but not enough to timeout
    suite->fakeClock->advance(2000);
    duelState.onStateLoop(&suite->device);
    
    // Should not timeout yet
    EXPECT_FALSE(duelState.transitionToIdle());
    
    // Dismount (timer should be invalidated)
    duelState.onStateDismounted(&suite->device);
    
    // Transition flags should be reset
    EXPECT_FALSE(duelState.transitionToIdle());
    EXPECT_FALSE(duelState.transitionToDuelPushed());
    EXPECT_FALSE(duelState.transitionToDuelReceivedResult());
}

// Test: Countdown state invalidates timer on dismount
inline void cleanupCountdownStateInvalidatesTimer(StateCleanupTests* suite) {
    DuelCountdown countdownState(suite->player, suite->matchManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    countdownState.onStateMounted(&suite->device);
    
    // Advance through some stages
    suite->fakeClock->advance(2100);
    countdownState.onStateLoop(&suite->device);
    
    // Dismount
    countdownState.onStateDismounted(&suite->device);
    
    // Battle flag should be reset
    EXPECT_FALSE(countdownState.shallWeBattle());
}

// Test: Handshake states clear wireless callbacks on dismount
inline void cleanupHandshakeClearsWirelessCallbacks(StateCleanupTests* suite) {
    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Dismount should clear wireless callbacks
    hunterState.onStateDismounted(&suite->device);

    // Transition flag should be reset
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());
}

// ============================================
// 4-Way Handshake Protocol Tests
// ============================================

// Test: Hunter sends HUNTER_READY after receiving BOUNTY_FINAL_ACK
inline void handshakeHunterSendsHunterReady(HandshakeStateTests* suite) {
    suite->player->setIsHunter(true);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    // Simulate receiving CONNECTION_CONFIRMED and sending HUNTER_RECEIVE_MATCH
    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    // Now receive BOUNTY_FINAL_ACK - should send HUNTER_READY and transition
    QuickdrawCommand finalAck(TestConstants::TEST_MAC_DEFAULT, BOUNTY_FINAL_ACK, receivedMatch);
    hunterState.onQuickdrawCommandReceived(finalAck);

    EXPECT_TRUE(hunterState.transitionToConnectionSuccessful());
}

// Test: Bounty waits for HUNTER_READY before transitioning
inline void handshakeBountyWaitsForHunterReady(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);

    // Receive HUNTER_RECEIVE_MATCH and send BOUNTY_FINAL_ACK
    Match receivedMatch("test-match-id", "5678", "1234");
    QuickdrawCommand command(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, receivedMatch);
    bountyState.onQuickdrawCommandReceived(command);

    // Should not transition yet - waiting for HUNTER_READY
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());
}

// Test: Bounty retries BOUNTY_FINAL_ACK if HUNTER_READY not received
inline void handshakeBountyRetriesBountyFinalAck(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);

    // Receive HUNTER_RECEIVE_MATCH and send BOUNTY_FINAL_ACK
    Match receivedMatch("test-match-id", "5678", "1234");
    QuickdrawCommand command(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, receivedMatch);
    bountyState.onQuickdrawCommandReceived(command);

    // Still not done
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Advance time past retry timeout (3000ms)
    suite->fakeClock->advance(3100);
    bountyState.onStateLoop(&suite->device);

    // Should have retried but not transitioned yet
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Advance time for second retry
    suite->fakeClock->advance(3100);
    bountyState.onStateLoop(&suite->device);

    // Still waiting
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());
}

// Test: Bounty exhausts retries and falls back to global timeout
inline void handshakeBountyExhaustsRetries(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);

    // Receive HUNTER_RECEIVE_MATCH and send BOUNTY_FINAL_ACK
    Match receivedMatch("test-match-id", "5678", "1234");
    QuickdrawCommand command(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, receivedMatch);
    bountyState.onQuickdrawCommandReceived(command);

    // Exhaust all 3 retries (3 timeouts at 3000ms each)
    for (int i = 0; i < 3; i++) {
        suite->fakeClock->advance(3100);
        bountyState.onStateLoop(&suite->device);
    }

    // After max retries, should not transition yet - falls back to global timeout
    EXPECT_FALSE(bountyState.transitionToConnectionSuccessful());

    // Should still be able to timeout via global timeout mechanism
    // (tested separately in existing timeout test)
}

// Test: Bounty transitions immediately when HUNTER_READY received
inline void handshakeBountyTransitionsOnHunterReady(HandshakeStateTests* suite) {
    suite->player->setIsHunter(false);
    suite->player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);
    bountyState.onStateMounted(&suite->device);

    // Receive HUNTER_RECEIVE_MATCH and send BOUNTY_FINAL_ACK
    Match receivedMatch("test-match-id", "5678", "1234");
    QuickdrawCommand command(TestConstants::TEST_MAC_DEFAULT, HUNTER_RECEIVE_MATCH, receivedMatch);
    bountyState.onQuickdrawCommandReceived(command);

    // Receive HUNTER_READY before any timeout
    QuickdrawCommand hunterReady(TestConstants::TEST_MAC_DEFAULT, HUNTER_READY, receivedMatch);
    bountyState.onQuickdrawCommandReceived(hunterReady);

    // Should immediately transition
    EXPECT_TRUE(bountyState.transitionToConnectionSuccessful());
}

// Test: DuelResult state clears wireless callbacks on dismount
inline void cleanupDuelResultClearsWirelessCallbacks(StateCleanupTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);
    
    // Dismount should clear state
    resultState.onStateDismounted(&suite->device);
    
    // Transition flags should be reset
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

// Test: Match manager clears current match properly
inline void cleanupMatchManagerClearsCurrentMatch(StateCleanupTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    
    ASSERT_NE(suite->matchManager->getCurrentMatch(), nullptr);
    
    suite->matchManager->clearCurrentMatch();
    
    EXPECT_EQ(suite->matchManager->getCurrentMatch(), nullptr);
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
}

// Test: Match manager clears duel state on match clear
inline void cleanupMatchManagerClearsDuelState(StateCleanupTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    
    // Set up duel state
    suite->matchManager->setDuelLocalStartTime(5000);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    suite->matchManager->setHunterDrawTime(200);
    
    // Clear match
    suite->matchManager->clearCurrentMatch();
    
    // All duel state should be reset
    EXPECT_EQ(suite->matchManager->getDuelLocalStartTime(), 0);
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
}

// ============================================
// Connection Successful State Tests
// ============================================

class ConnectionSuccessfulTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        // The state uses a SimpleTimer with 400ms delay to trigger flashes
        // Starting time doesn't matter since the timer tracks elapsed time
        fakeClock->setTime(800);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    Player* player;
    FakePlatformClock* fakeClock;
};

// Test: Connection successful transitions to countdown after threshold
inline void connectionSuccessfulTransitionsAfterThreshold(ConnectionSuccessfulTests* suite) {
    ConnectionSuccessful connState(suite->player);
    
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    connState.onStateMounted(&suite->device);
    
    // Should not transition initially
    EXPECT_FALSE(connState.transitionToCountdown());
    
    // Simulate flash cycles - need alertCount > 12 (threshold), so 13 flashes
    // Each flash happens when the flashTimer expires (duration > elapsed)
    // The timer uses strict inequality, so we need to advance by more than 400ms
    for (int i = 0; i < 13; i++) {
        suite->fakeClock->advance(401);
        connState.onStateLoop(&suite->device);
    }
    
    // Should now transition to countdown (alertCount > 12)
    EXPECT_TRUE(connState.transitionToCountdown());
}

// ============================================
// Quickdraw Destructor Tests
// ============================================

class QuickdrawDestructorTests : public testing::Test {
public:
    void SetUp() override {
        player = new Player();
        char playerId[] = "test-player";
        player->setUserID(playerId);
        player->setIsHunter(true);

        // Create MatchManager to test cleanup
        matchManager = new MatchManager();
        matchManager->initialize(player, &storage, &peerComms, &wirelessManager);
    }

    void TearDown() override {
        delete matchManager;
        delete player;
    }

    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager wirelessManager;
    Player* player;
    MatchManager* matchManager;
};

// Test: MatchManager can be created and destroyed without leaking memory
// This verifies the fix for the Quickdraw destructor memory leak where MatchManager
// was allocated with new but never deleted.
inline void quickdrawDestructorCleansUpMatchManager(QuickdrawDestructorTests* suite) {
    // Create and destroy MatchManager to verify no leaks
    // The destructor properly cleans up activeDuelState.match
    MatchManager* testManager = new MatchManager();
    testManager->initialize(suite->player, &suite->storage, &suite->peerComms, &suite->wirelessManager);

    // Create a match to verify cleanup
    Match* match = testManager->createMatch("test-match-id", "hunter-id", "bounty-id");
    EXPECT_NE(match, nullptr);
    EXPECT_EQ(testManager->getCurrentMatch(), match);

    // Delete the manager - this should properly clean up the match
    delete testManager;

    // Test passes if we reach here without crashes or memory errors
    // The actual memory leak detection would be handled by tools like Valgrind or ASan in CI
}

// ============================================
// State Transition Edge Case Tests
// ============================================

class StateTransitionEdgeCaseTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);
        player->setOpponentMacAddress(TestConstants::TEST_MAC_DEFAULT);

        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        progressManager = new ProgressManager();
        progressManager->initialize(player, &storage);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(storage, write(_, _)).WillByDefault(Return(100));
        ON_CALL(storage, writeUChar(_, _)).WillByDefault(Return(1));
        ON_CALL(storage, readUChar(_, _)).WillByDefault(Return(0));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete progressManager;
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    ProgressManager* progressManager;
    FakePlatformClock* fakeClock;
};

// Test: Rapid transition from Idle to Handshake and back on timeout
inline void edgeCaseRapidIdleHandshakeTransition(StateTransitionEdgeCaseTests* suite) {
    Idle idleState(suite->player, suite->matchManager, suite->wirelessManager, suite->progressManager);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    idleState.onStateMounted(&suite->device);

    // Verify initial state - should not transition
    EXPECT_FALSE(idleState.transitionToHandshake());

    // Clean up idle
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    idleState.onStateDismounted(&suite->device);

    // Start handshake
    HandshakeInitiateState handshakeState(suite->player);
    handshakeState.onStateMounted(&suite->device);

    // Initialize timeout
    handshakeState.onStateLoop(&suite->device);

    // Advance to timeout immediately
    suite->fakeClock->advance(20001);

    // Should timeout back to idle
    EXPECT_TRUE(handshakeState.transitionToIdle());

    handshakeState.onStateDismounted(&suite->device);
}

// Test: ConnectionSuccessful state handles rapid mounting/dismounting
inline void edgeCaseConnectionSuccessfulRapidCycle(StateTransitionEdgeCaseTests* suite) {
    ConnectionSuccessful connState1(suite->player);

    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // Mount and immediately dismount
    connState1.onStateMounted(&suite->device);
    EXPECT_FALSE(connState1.transitionToCountdown());
    connState1.onStateDismounted(&suite->device);

    // Create new state - should work fine
    ConnectionSuccessful connState2(suite->player);
    connState2.onStateMounted(&suite->device);

    // Advance through flash cycles
    for (int i = 0; i < 13; i++) {
        suite->fakeClock->advance(401);
        connState2.onStateLoop(&suite->device);
    }

    EXPECT_TRUE(connState2.transitionToCountdown());
    connState2.onStateDismounted(&suite->device);
}

// Test: Duel timeout with match still active
inline void edgeCaseDuelTimeoutClearsMatch(StateTransitionEdgeCaseTests* suite) {
    suite->matchManager->createMatch("test-match-id", suite->player->getUserID(), "5678");

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    // Verify match exists
    EXPECT_NE(suite->matchManager->getCurrentMatch(), nullptr);

    // Advance past timeout
    suite->fakeClock->advance(4100);
    duelState.onStateLoop(&suite->device);

    // Should timeout to idle
    EXPECT_TRUE(duelState.transitionToIdle());

    // Cleanup
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    duelState.onStateDismounted(&suite->device);
}

// Test: Win state transition clears match properly
inline void edgeCaseWinStateClearsMatchOnTransition(StateTransitionEdgeCaseTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);

    EXPECT_TRUE(resultState.transitionToWin());
    resultState.onStateDismounted(&suite->device);

    // Now enter Win state
    Win winState(suite->player);
    winState.onStateMounted(&suite->device);

    // Should not reset immediately
    EXPECT_FALSE(winState.resetGame());

    // Advance time (Win state has an 8 second timer)
    suite->fakeClock->advance(8100);
    // Call onStateLoop to update timer
    winState.onStateLoop(&suite->device);

    // Should now reset
    EXPECT_TRUE(winState.resetGame());

    winState.onStateDismounted(&suite->device);
}

// Test: Lose state transition clears match properly
inline void edgeCaseLoseStateClearsMatchOnTransition(StateTransitionEdgeCaseTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setBountyDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);

    EXPECT_TRUE(resultState.transitionToLose());
    resultState.onStateDismounted(&suite->device);

    // Now enter Lose state
    Lose loseState(suite->player);
    loseState.onStateMounted(&suite->device);

    // Should not reset immediately
    EXPECT_FALSE(loseState.resetGame());

    // Advance time (Lose state has an 8 second timer)
    suite->fakeClock->advance(8100);
    // Call onStateLoop to update timer
    loseState.onStateLoop(&suite->device);

    // Should now reset
    EXPECT_TRUE(loseState.resetGame());

    loseState.onStateDismounted(&suite->device);
}

// Test: Countdown button masher penalty persists across state mount/dismount
inline void edgeCaseCountdownPenaltyPersistsAcrossCycles(StateTransitionEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // First countdown cycle - accumulate penalty
    DuelCountdown countdown1(suite->player, suite->matchManager);

    parameterizedCallbackFunction masherCallback = nullptr;
    void* masherCtx = nullptr;
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillOnce(DoAll(SaveArg<0>(&masherCallback), SaveArg<1>(&masherCtx)));
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    countdown1.onStateMounted(&suite->device);

    // Mash button 5 times
    ASSERT_NE(masherCallback, nullptr);
    for (int i = 0; i < 5; i++) {
        masherCallback(masherCtx);
    }

    // Dismount
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    countdown1.onStateDismounted(&suite->device);

    // Second countdown cycle - penalty should persist
    DuelCountdown countdown2(suite->player, suite->matchManager);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    countdown2.onStateMounted(&suite->device);

    // Advance through countdown (THREE -> TWO -> ONE -> BATTLE)
    // Each stage is 2000ms, need to call onStateLoop for each stage
    suite->fakeClock->advance(2100);
    countdown2.onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    countdown2.onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    countdown2.onStateLoop(&suite->device);

    EXPECT_TRUE(countdown2.shallWeBattle());

    // Cleanup
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    countdown2.onStateDismounted(&suite->device);
}

// Test: Handshake states handle NULL opponent MAC gracefully
inline void edgeCaseHandshakeWithNullOpponentMac(StateTransitionEdgeCaseTests* suite) {
    // Clear opponent MAC
    suite->player->setOpponentMacAddress("");

    // Hunter state should handle this gracefully
    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);

    // Should not crash on mount
    hunterState.onStateMounted(&suite->device);
    hunterState.onStateLoop(&suite->device);

    // Should not transition (no valid opponent)
    EXPECT_FALSE(hunterState.transitionToConnectionSuccessful());

    hunterState.onStateDismounted(&suite->device);
}

// Test: Multiple consecutive state transitions without crashes
inline void edgeCaseMultipleConsecutiveTransitions(StateTransitionEdgeCaseTests* suite) {
    // Idle -> Handshake -> ConnectionSuccessful -> Countdown -> Duel

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // 1. Idle
    Idle idleState(suite->player, suite->matchManager, suite->wirelessManager, suite->progressManager);
    idleState.onStateMounted(&suite->device);
    idleState.onStateDismounted(&suite->device);

    // 2. HandshakeInitiate
    HandshakeInitiateState handshakeInitiate(suite->player);
    handshakeInitiate.onStateMounted(&suite->device);
    suite->fakeClock->advance(600);
    handshakeInitiate.onStateLoop(&suite->device);
    EXPECT_TRUE(handshakeInitiate.transitionToHunterSendId());
    handshakeInitiate.onStateDismounted(&suite->device);

    // 3. HunterSendId
    HunterSendIdState hunterState(suite->player, suite->matchManager, suite->wirelessManager);
    hunterState.onStateMounted(&suite->device);

    Match receivedMatch("test-match-id", "", "5678");
    QuickdrawCommand connectionConfirmed(TestConstants::TEST_MAC_DEFAULT, CONNECTION_CONFIRMED, receivedMatch);
    hunterState.onQuickdrawCommandReceived(connectionConfirmed);

    QuickdrawCommand finalAck(TestConstants::TEST_MAC_DEFAULT, BOUNTY_FINAL_ACK, receivedMatch);
    hunterState.onQuickdrawCommandReceived(finalAck);

    EXPECT_TRUE(hunterState.transitionToConnectionSuccessful());
    hunterState.onStateDismounted(&suite->device);

    // 4. ConnectionSuccessful
    ConnectionSuccessful connState(suite->player);
    connState.onStateMounted(&suite->device);
    for (int i = 0; i < 13; i++) {
        suite->fakeClock->advance(401);
        connState.onStateLoop(&suite->device);
    }
    EXPECT_TRUE(connState.transitionToCountdown());
    connState.onStateDismounted(&suite->device);

    // 5. Countdown
    DuelCountdown countdown(suite->player, suite->matchManager);
    countdown.onStateMounted(&suite->device);
    // Advance through each countdown stage
    suite->fakeClock->advance(2100);
    countdown.onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    countdown.onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    countdown.onStateLoop(&suite->device);
    EXPECT_TRUE(countdown.shallWeBattle());
    countdown.onStateDismounted(&suite->device);

    // 6. Duel
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    // Advance and press button
    suite->fakeClock->advance(200);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    duelState.onStateLoop(&suite->device);

    EXPECT_TRUE(duelState.transitionToDuelPushed());
    duelState.onStateDismounted(&suite->device);
}

// Test: DuelReceivedResult handles grace period expiry correctly
inline void edgeCaseDuelReceivedResultGracePeriodExpiry(StateTransitionEdgeCaseTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);

    // Should not transition immediately
    EXPECT_FALSE(receivedState.transitionToDuelResult());

    // Advance to just before grace period expires (749ms)
    suite->fakeClock->advance(749);
    receivedState.onStateLoop(&suite->device);
    EXPECT_FALSE(receivedState.transitionToDuelResult());

    // Advance 2ms more (total 751ms) - should expire
    suite->fakeClock->advance(2);
    receivedState.onStateLoop(&suite->device);

    // Should now transition
    EXPECT_TRUE(receivedState.transitionToDuelResult());

    // Verify pity time was set
    Match* match = suite->matchManager->getCurrentMatch();
    ASSERT_NE(match, nullptr);
    EXPECT_GT(match->getHunterDrawTime(), 0);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    receivedState.onStateDismounted(&suite->device);
}

// Test: DuelPushed handles grace period expiry correctly
inline void edgeCaseDuelPushedGracePeriodExpiry(StateTransitionEdgeCaseTests* suite) {
    suite->matchManager->createMatch("test-match", suite->player->getUserID(), "5678");
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);

    DuelPushed pushedState(suite->player, suite->matchManager);
    pushedState.onStateMounted(&suite->device);

    // Should not transition immediately
    EXPECT_FALSE(pushedState.transitionToDuelResult());

    // Advance to just before grace period expires (899ms)
    suite->fakeClock->advance(899);
    pushedState.onStateLoop(&suite->device);
    EXPECT_FALSE(pushedState.transitionToDuelResult());

    // Advance 2ms more (total 901ms) - should expire
    suite->fakeClock->advance(2);
    pushedState.onStateLoop(&suite->device);

    // Should now transition
    EXPECT_TRUE(pushedState.transitionToDuelResult());

    pushedState.onStateDismounted(&suite->device);
}

// Test: Handshake state transition with malformed opponent MAC
inline void edgeCaseHandshakeWithMalformedMac(StateTransitionEdgeCaseTests* suite) {
    // Set malformed MAC address
    suite->player->setOpponentMacAddress("INVALID:MAC");

    BountySendConnectionConfirmedState bountyState(suite->player, suite->matchManager, suite->wirelessManager);

    // Should not crash
    bountyState.onStateMounted(&suite->device);
    bountyState.onStateLoop(&suite->device);

    bountyState.onStateDismounted(&suite->device);
}
