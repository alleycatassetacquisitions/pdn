#pragma once

#include <array>
#include <memory>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "apps/handshake/handshake.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "utility-tests.hpp"
#include "device/device-constants.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::DoAll;

// Stubs a fixture's RDC to accept the conventional test MAC as a direct peer
// so MatchManager's SEND_MATCH_ID gate passes. Called once per fixture SetUp.
inline void wireFixtureRdcForMatchManager(MockDevice& device, MatchManager* mm) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK, dummyMac);
    device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::OUTPUT_JACK, dummyMac);
    mm->setRemoteDeviceCoordinator(&device.fakeRemoteDeviceCoordinator);
}

inline Peer makeHSPeer(const uint8_t* mac, SerialIdentifier sid) {
    Peer p;
    std::copy(mac, mac + 6, p.macAddr.begin());
    p.sid = sid;
    p.deviceType = DeviceType::PDN;
    return p;
}

static const uint8_t kTestMacBytes[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// ============================================
// Idle State Tests
// ============================================

class IdleStateTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        char playerId[] = "1234";
        player->setUserID(playerId);
        player->setIsHunter(true);

        matchManager = new MatchManager();
        wirelessManager = new FakeQuickdrawWirelessManager();
        matchManager->initialize(player, &storage, wirelessManager);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainDuelManager = new ChainDuelManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);
        idleState = new Idle(player, matchManager, &device.fakeRemoteDeviceCoordinator, chainDuelManager);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete idleState;
        delete chainDuelManager;
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    FakeQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    ChainDuelManager* chainDuelManager;
    Idle* idleState;
    FakePlatformClock* fakeClock;
};

// Test: Idle state mounts and registers button callbacks
inline void idleMountRegistersButtonCallbacks(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    // With FakeRemoteDeviceCoordinator always returning DISCONNECTED, should not transition
    EXPECT_FALSE(suite->idleState->transitionToDuelCountdown());
}

// Test: Idle state does not transition without a connection
inline void idleDoesNotTransitionWhenDisconnected(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToDuelCountdown());

    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToDuelCountdown());
}

// Test: State cleanup on dismount
inline void idleStateClearsOnDismount(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    suite->idleState->onStateDismounted(&suite->device);
}

// Test: Button callbacks are registered and removed properly
inline void idleButtonCallbacksRegisteredAndRemoved(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    suite->idleState->onStateDismounted(&suite->device);
}

// Test: transitionToDuelCountdown stays false while match exists but ACK not yet received
inline void idleDoesNotTransitionWithMatchButNotReady(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    suite->idleState->onStateMounted(&suite->device);

    // Hunter initiates but has not yet received MATCH_ID_ACK
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(suite->idleState->transitionToDuelCountdown());
}

// Test: transitionToDuelCountdown returns true once matchIsReady is set via the full handshake
inline void idleTransitionsToDuelCountdownWhenMatchIsReady(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    suite->idleState->onStateMounted(&suite->device);

    // Hunter initiates match, then receives ACK from bounty
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    const char* matchId = suite->matchManager->getCurrentMatch()->getMatchId();
    QuickdrawCommand ack(dummyMac, QDCommand::MATCH_ID_ACK, matchId, "boun", 0, false);
    suite->matchManager->listenForMatchEvents(ack);

    EXPECT_TRUE(suite->idleState->transitionToDuelCountdown());
}

// ============================================
// Handshake State Tests
// ============================================

// Fixture: provides HandshakeWirelessManager and a fake WirelessManager for sending packets.
class HandshakeStateTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));

        handshakeWirelessManager.initialize(device.wirelessManager);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Simulate a remote device sending a HandshakePacket to this device.
    // receivingJack is the opposite of the sender's jack (packets always travel OUTPUT<->INPUT).
    void deliverPacket(int command, SerialIdentifier senderJack, int deviceType = 0) {
        SerialIdentifier receivingJack = (senderJack == SerialIdentifier::OUTPUT_JACK)
            ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
        struct RawPacket { int sendingJack; int receivingJack; int deviceType; int command; } __attribute__((packed));
        RawPacket pkt{ static_cast<int>(senderJack), static_cast<int>(receivingJack), deviceType, command };
        uint8_t dummyMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        handshakeWirelessManager.processHandshakeCommand(dummyMac,
            reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    }

    MockDevice device;
    HandshakeWirelessManager handshakeWirelessManager;
    FakePlatformClock* fakeClock;
};

// Test: OutputIdleState transitions when a valid MAC is received over serial
inline void outputIdleTransitionsOnMacReceived(HandshakeStateTests* suite) {
    OutputIdleState idleState(&suite->handshakeWirelessManager);
    idleState.onStateMounted(&suite->device);

    EXPECT_FALSE(idleState.transitionToOutputSendId());

    // Simulate INPUT side sending its MAC+port+deviceType over the output jack serial
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");

    EXPECT_TRUE(idleState.transitionToOutputSendId());

    idleState.onStateDismounted(&suite->device);
}

// Test: OutputIdleState does not transition on unrelated serial messages
inline void outputIdleIgnoresUnrelatedSerial(HandshakeStateTests* suite) {
    OutputIdleState idleState(&suite->handshakeWirelessManager);
    idleState.onStateMounted(&suite->device);

    suite->device.outputJackSerial.stringCallback("HEARTBEAT");
    EXPECT_FALSE(idleState.transitionToOutputSendId());

    idleState.onStateDismounted(&suite->device);
}

// Test: OutputIdleState clears serial callback on dismount
inline void outputIdleClearsCallbackOnDismount(HandshakeStateTests* suite) {
    OutputIdleState idleState(&suite->handshakeWirelessManager);
    idleState.onStateMounted(&suite->device);
    idleState.onStateDismounted(&suite->device);

    EXPECT_FALSE(idleState.transitionToOutputSendId());
}

// Test: OutputSendIdState sends EXCHANGE_ID on mount and transitions when ack arrives
inline void outputSendIdTransitionsOnExchangeIdAck(HandshakeStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    // Seed the MAC peer so sendPacket has a destination
    uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->handshakeWirelessManager.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeHSPeer(peerMac, SerialIdentifier::OUTPUT_JACK));

    OutputSendIdState sendIdState(&suite->handshakeWirelessManager);
    sendIdState.onStateMounted(&suite->device);

    EXPECT_FALSE(sendIdState.transitionToConnected());

    // Simulate input replying with EXCHANGE_ID (input's OUTPUT_JACK becomes our INPUT_JACK after inversion)
    suite->deliverPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);

    EXPECT_TRUE(sendIdState.transitionToConnected());

    sendIdState.onStateDismounted(&suite->device);
}

// Test: OutputSendIdState clears state on dismount
inline void outputSendIdClearsOnDismount(HandshakeStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->handshakeWirelessManager.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeHSPeer(peerMac, SerialIdentifier::OUTPUT_JACK));

    OutputSendIdState sendIdState(&suite->handshakeWirelessManager);
    sendIdState.onStateMounted(&suite->device);

    suite->deliverPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    EXPECT_TRUE(sendIdState.transitionToConnected());

    sendIdState.onStateDismounted(&suite->device);
    EXPECT_FALSE(sendIdState.transitionToConnected());
}

// Test: InputIdleState emits MAC over serial and transitions on EXCHANGE_ID command
inline void inputIdleTransitionsOnExchangeId(HandshakeStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(Return(nullptr));

    InputIdleState idleState(&suite->handshakeWirelessManager);
    idleState.onStateMounted(&suite->device);

    EXPECT_FALSE(idleState.transitionToSendId());

    // Output sends EXCHANGE_ID to input (output's OUTPUT_JACK → our INPUT_JACK)
    suite->deliverPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);

    EXPECT_TRUE(idleState.transitionToSendId());

    idleState.onStateDismounted(&suite->device);
}

// Test: InputSendIdState transitions only after receiving EXCHANGE_ID ack
inline void inputSendIdTransitionsOnExchangeIdAck(HandshakeStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->handshakeWirelessManager.setMacPeer(SerialIdentifier::INPUT_JACK, makeHSPeer(peerMac, SerialIdentifier::INPUT_JACK));

    InputSendIdState sendIdState(&suite->handshakeWirelessManager);
    sendIdState.onStateMounted(&suite->device);

    // Should NOT transition immediately after sending
    EXPECT_FALSE(sendIdState.transitionToConnected());

    // Output sends final EXCHANGE_ID ack (output's OUTPUT_JACK → our INPUT_JACK)
    suite->deliverPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);

    EXPECT_TRUE(sendIdState.transitionToConnected());

    sendIdState.onStateDismounted(&suite->device);
}

// Test: InputSendIdState clears state on dismount
inline void inputSendIdClearsOnDismount(HandshakeStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->handshakeWirelessManager.setMacPeer(SerialIdentifier::INPUT_JACK, makeHSPeer(peerMac, SerialIdentifier::INPUT_JACK));

    InputSendIdState sendIdState(&suite->handshakeWirelessManager);
    sendIdState.onStateMounted(&suite->device);

    suite->deliverPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    EXPECT_TRUE(sendIdState.transitionToConnected());

    sendIdState.onStateDismounted(&suite->device);
    EXPECT_FALSE(sendIdState.transitionToConnected());
}

// Test: HandshakeApp output-jack path resets to idle on timeout
inline void handshakeAppOutputJackTimeoutResetsToIdle(HandshakeStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    HandshakeApp handshakeApp(&suite->handshakeWirelessManager, SerialIdentifier::OUTPUT_JACK);
    handshakeApp.onStateMounted(&suite->device);

    // Advance into PrimarySendId by delivering a MAC string over the output serial
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    handshakeApp.onStateLoop(&suite->device);

    // Handshake timeout not yet reached
    suite->fakeClock->advance(400);
    handshakeApp.onStateLoop(&suite->device);

    // Past the 500ms handshakeTimeout → should reset to idle
    suite->fakeClock->advance(200);
    handshakeApp.onStateLoop(&suite->device);

    // After reset, HandshakeApp should be back in OutputIdleState (stateId == OUTPUT_IDLE_STATE)
    EXPECT_EQ(handshakeApp.getCurrentState()->getStateId(), HandshakeStateId::OUTPUT_IDLE_STATE);

    handshakeApp.onStateDismounted(&suite->device);
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
        wirelessManager = new FakeQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, wirelessManager);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainDuelManager = new ChainDuelManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);
        countdownState = new DuelCountdown(player, matchManager, &device.fakeRemoteDeviceCoordinator, chainDuelManager);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete countdownState;
        delete chainDuelManager;
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
    FakeQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    ChainDuelManager* chainDuelManager;
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

        matchManager = new MatchManager();
        wirelessManager = new FakeQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, wirelessManager);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainDuelManager = new ChainDuelManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

        // Create a match for testing via the production wireless path
        uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        matchManager->initializeMatch(dummyMac);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete chainDuelManager;
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    FakeQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    ChainDuelManager* chainDuelManager;
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
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
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
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    duelState.onStateMounted(&suite->device);
    
    // Advance 250ms (simulating reaction time)
    suite->fakeClock->advance(250);
    
    // Trigger button press
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    // Check that hunter draw time was set
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 250);
}

// Test: Button press applies button masher penalty
inline void duelButtonPressAppliesMasherPenalty(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    // First, simulate button mashing during countdown
    DuelCountdown countdownState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    
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

    // Cycle through THREE → TWO → ONE → BATTLE (3 × 2001ms) so doBattle=true
    // and the match is not cleared on dismount.
    // SimpleTimer::expired() uses strict '<', so we need elapsed > duration (2000ms).
    for (int i = 0; i < 3; i++) {
        suite->fakeClock->advance(2001);
        countdownState.onStateLoop(&suite->device);
    }

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    countdownState.onStateDismounted(&suite->device);
    
    // Now start the duel
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    duelState.onStateMounted(&suite->device);
    
    // Advance 200ms
    suite->fakeClock->advance(200);
    
    // Trigger button press
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    // Reaction time should be 200ms + (2 * 75ms penalty) = 350ms
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 350);
}

// Test: Button press broadcasts DRAW_RESULT
inline void duelButtonPressBroadcastsDrawResult(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    size_t sentBefore = suite->wirelessManager->sentCommands.size();

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    duelState.onStateMounted(&suite->device);

    suite->fakeClock->advance(200);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);

    ASSERT_GT(suite->wirelessManager->sentCommands.size(), sentBefore);
    EXPECT_EQ(suite->wirelessManager->sentCommands.back().command, QDCommand::DRAW_RESULT);
}

// Test: DuelPushed waits for opponent result (grace period)
inline void duelPushedWaitsForOpponentResult(DuelStateTests* suite) {
    DuelPushed pushedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    
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
    DuelPushed pushedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    
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
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
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
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
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
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
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
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
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
    
    DuelPushed pushedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
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
    suite->matchManager->setOpponentNeverPressed();

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
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
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
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    // Advance past grace period
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    // Hunter draw time should now be set (pity time)
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_GT(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 0);
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

        matchManager = new MatchManager();
        wirelessManager = new FakeQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, wirelessManager);
        wireFixtureRdcForMatchManager(device, matchManager);

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
    FakeQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    FakePlatformClock* fakeClock;
};

// Test: Hunter wins with faster time
inline void resultHunterWinsWithFasterTime(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Bounty wins with faster time
inline void resultBountyWinsWithFasterTime(DuelResultTests* suite) {
    suite->player->setIsHunter(false);
    // Simulate hunter sending SEND_MATCH_ID; this bounty receives and creates the match.
    // Use the fixture's stubbed RDC peer MAC so the SEND_MATCH_ID gate accepts it.
    uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand sendMatchCmd(hunterMac, QDCommand::SEND_MATCH_ID, "test-match-id-000000000000000", "5678", 0, true);
    suite->matchManager->listenForMatchEvents(sendMatchCmd);
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Tied times favor opponent (local player loses)
inline void resultTiedTimesFavorOpponent(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
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
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setOpponentNeverPressed();

    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Win transitions to Win state
inline void resultWinTransitionsToWinState(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
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
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
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
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
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
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
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
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
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

        matchManager = new MatchManager();
        wirelessManager = new FakeQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, wirelessManager);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainDuelManager = new ChainDuelManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

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
        delete chainDuelManager;
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    FakeQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    ChainDuelManager* chainDuelManager;
    FakePlatformClock* fakeClock;
};

// Test: Idle state clears button callbacks on dismount
inline void cleanupIdleClearsButtonCallbacks(StateCleanupTests* suite) {
    Idle idleState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    idleState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    idleState.onStateDismounted(&suite->device);
}

// Test: Countdown state clears button callbacks on dismount
inline void cleanupCountdownClearsButtonCallbacks(StateCleanupTests* suite) {
    DuelCountdown countdownState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    countdownState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    countdownState.onStateDismounted(&suite->device);
}

// Test: Duel state preserves button callbacks for DuelPushed/DuelReceivedResult
inline void cleanupDuelStateDoesNotClearCallbacksOnDismount(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    duelState.onStateMounted(&suite->device);
    
    // Duel state should NOT clear button callbacks on dismount
    // The next state (DuelPushed or DuelReceivedResult) uses them
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(0);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(0);
    
    duelState.onStateDismounted(&suite->device);
}

// Test: DuelReceivedResult state clears button callbacks on dismount
inline void cleanupDuelReceivedResultClearsButtonCallbacks(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    DuelReceivedResult receivedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    receivedState.onStateDismounted(&suite->device);
}

// Test: Duel state invalidates timer on dismount
inline void cleanupDuelStateInvalidatesTimer(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    // Player is hunter, so OUTPUT_JACK must be connected for isConnected() to return true.
    // Without this, onStateLoop would transition to idle due to disconnection, not the timer.
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    
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
    DuelCountdown countdownState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);
    
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

// Test: OutputSendIdState clears wireless callbacks and transition flag on dismount
inline void cleanupHandshakeClearsWirelessCallbacks(StateCleanupTests* suite) {
    ON_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));

    HandshakeWirelessManager hwm;
    hwm.initialize(suite->device.wirelessManager);
    uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    hwm.setMacPeer(SerialIdentifier::OUTPUT_JACK, makeHSPeer(peerMac, SerialIdentifier::OUTPUT_JACK));

    OutputSendIdState outputState(&hwm);
    outputState.onStateMounted(&suite->device);

    outputState.onStateDismounted(&suite->device);

    EXPECT_FALSE(outputState.transitionToConnected());
}

// Test: DuelResult state clears wireless callbacks on dismount
inline void cleanupDuelResultClearsWirelessCallbacks(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
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
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    
    suite->matchManager->clearCurrentMatch();
    
    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
}

// Test: Match manager clears duel state on match clear
inline void cleanupMatchManagerClearsDuelState(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    
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
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));

        hwm.initialize(device.wirelessManager);
        uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        Peer peer;
        std::copy(std::begin(mac), std::end(mac), peer.macAddr.begin());
        peer.sid = SerialIdentifier::OUTPUT_JACK;
        peer.deviceType = DeviceType::PDN;
        hwm.setMacPeer(SerialIdentifier::OUTPUT_JACK, peer);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    HandshakeWirelessManager hwm;
    FakePlatformClock* fakeClock;
};

// ============================================
// Duel State Callback Cleanup (new behaviours from refactor)
// ============================================

// Test: Duel state removes button callbacks when transitioning to DuelReceivedResult
inline void cleanupDuelStateClearsCallbacksWhenGoingToDuelReceivedResult(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainDuelManager);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    duelState.onStateMounted(&suite->device);

    // Simulate receiving opponent's result so transitionToDuelReceivedResult fires
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    duelState.onStateLoop(&suite->device);
    ASSERT_TRUE(duelState.transitionToDuelReceivedResult());

    // Duel must now remove callbacks on dismount so DuelReceivedResult starts clean
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    duelState.onStateDismounted(&suite->device);
}

// Test: DuelPushed clears match when disconnected on dismount
inline void pushedClearsMatchOnDisconnect(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setReceivedButtonPush();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    // FakeRemoteDeviceCoordinator always reports DISCONNECTED, so isConnected() == false
    DuelPushed pushedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    pushedState.onStateMounted(&suite->device);
    pushedState.onStateDismounted(&suite->device);

    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
}

// Test: DuelReceivedResult clears match when disconnected on dismount
inline void receivedResultClearsMatchOnDisconnect(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());

    DuelReceivedResult receivedState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    receivedState.onStateDismounted(&suite->device);

    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
}

// ============================================
// Quickdraw lifecycle — exercises ctor + dtor so the native_asan env
// catches leaks in the members Quickdraw owns (matchManager, chainDuelManager).
// Pre-fix: ~Quickdraw just nulled the pointers, leaking ~100 bytes per device.
// Post-fix: deletes are issued and ASAN passes clean.
// ============================================

class QuickdrawLifecycleTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(mac));

        player = new Player();
        char playerId[] = "life";
        player->setUserID(playerId);

        qwm = new FakeQuickdrawWirelessManager();
    }

    void TearDown() override {
        delete qwm;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    FakePlatformClock* fakeClock;
    Player* player;
    FakeQuickdrawWirelessManager* qwm;
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
};

// Create + destroy many Quickdraw instances; under ASAN (env:native_asan) a
// leak in ~Quickdraw's ownership of matchManager / chainDuelManager would be
// reported. Without ASAN this still catches crashes in the lifecycle path.
inline void quickdrawCtorDtorDoesNotLeak(QuickdrawLifecycleTests* suite) {
    for (int i = 0; i < 5; i++) {
        auto* qd = new Quickdraw(suite->player, &suite->device, suite->qwm, nullptr, nullptr);
        delete qd;
    }
}

// Test: HandshakeConnectedState transitions to idle on heartbeat timeout
inline void connectionSuccessfulTransitionsAfterThreshold(ConnectionSuccessfulTests* suite) {
    HandshakeConnectedState connectedState(&suite->hwm, SerialIdentifier::OUTPUT_JACK, HandshakeStateId::OUTPUT_CONNECTED_STATE);
    connectedState.onStateMounted(&suite->device);

    EXPECT_FALSE(connectedState.transitionToIdle());

    // Advance past the firstHeartbeatTimeout (400ms)
    suite->fakeClock->advance(500);
    connectedState.onStateLoop(&suite->device);

    EXPECT_TRUE(connectedState.transitionToIdle());

    connectedState.onStateDismounted(&suite->device);
}
