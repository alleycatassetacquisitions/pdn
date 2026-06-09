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
#include "wireless/direct-peer-table.hpp"
#include "utility-tests.hpp"

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
        matchManager->initialize(player, &storage, &wireless.transport);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainManager = new ChainManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);
        idleState = new Idle(player, matchManager, &device.fakeRemoteDeviceCoordinator, chainManager);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete idleState;
        delete chainManager;
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    QuickdrawTestStack wireless;
    Player* player;
    MatchManager* matchManager;
    ChainManager* chainManager;
    Idle* idleState;
    FakePlatformClock* fakeClock;
};

inline void idleMountRegistersButtonCallbacks(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);
}

inline void idleDoesNotTransitionWhenDisconnected(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToDuelCountdown());

    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToDuelCountdown());
}

inline void idleStateClearsOnDismount(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    suite->idleState->onStateDismounted(&suite->device);
}

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

inline void idleTransitionsToDuelCountdownWhenMatchIsReady(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    suite->idleState->onStateMounted(&suite->device);

    // Hunter initiates match, then receives ACK from bounty
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    const char* matchId = suite->matchManager->getCurrentMatch()->getMatchId();
    QuickdrawPacket ack = makeQuickdrawPacket(QDCommand::MATCH_ID_ACK, matchId, "boun", 0, false);
    suite->matchManager->listenForMatchEvents(dummyMac, ack);

    EXPECT_TRUE(suite->idleState->transitionToDuelCountdown());
}

// Once a match is ready, re-running Idle::onStateLoop must not disturb it:
// initializeMatch early-returns while a match exists, so repeated ticks neither
// regenerate nor clear the ready match before the DuelCountdown transition fires.
inline void idleKeepsReadyMatchAcrossReinit(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());

    // Connected hunter with a PDN bounty on the opponent (OUTPUT) jack, so
    // Idle::onStateLoop initializes a match.
    uint8_t bountyMac[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    suite->device.fakeRemoteDeviceCoordinator.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::PDN);
    suite->device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::OUTPUT_JACK, bountyMac);
    // Opponent-jack peer is a bounty (opposite role) as a confirmed mutual edge:
    // the BEACON-flood derivation canInitiateMatch now reads.
    suite->device.fakeRemoteDeviceCoordinator.setMutualOpponentIsHunter(false);

    suite->idleState->onStateMounted(&suite->device);
    suite->idleState->onStateLoop(&suite->device);
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value())
        << "Idle did not initialize a match; connectivity/canInitiateMatch setup is off";

    // Ack arrives, making the match ready (execDrivers applies it before onStateLoop in firmware).
    const char* matchId = suite->matchManager->getCurrentMatch()->getMatchId();
    QuickdrawPacket ack = makeQuickdrawPacket(QDCommand::MATCH_ID_ACK, matchId, "boun", 0, false);
    suite->matchManager->listenForMatchEvents(bountyMac, ack);
    ASSERT_TRUE(suite->matchManager->isMatchReady());

    // A subsequent tick re-enters the init path; the ready match must survive it.
    suite->idleState->onStateLoop(&suite->device);

    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value())
        << "ready match was cleared by a re-init tick";
    EXPECT_TRUE(suite->idleState->transitionToDuelCountdown());
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
        matchManager->initialize(player, &storage, &wireless.transport);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainManager = new ChainManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);
        countdownState = new DuelCountdown(player, matchManager, &device.fakeRemoteDeviceCoordinator, chainManager);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete countdownState;
        delete chainManager;
        delete matchManager;
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
    QuickdrawTestStack wireless;
    Player* player;
    MatchManager* matchManager;
    ChainManager* chainManager;
    DuelCountdown* countdownState;
    FakePlatformClock* fakeClock;
};

inline void countdownProgressesThroughStages(DuelCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    suite->countdownState->onStateMounted(&suite->device);
    
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
    
    EXPECT_TRUE(suite->countdownState->shallWeBattle());
}

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
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    suite->countdownState->onStateDismounted(&suite->device);
    
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
        matchManager->initialize(player, &storage, &wireless.transport);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainManager = new ChainManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

        uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        matchManager->initializeMatch(dummyMac);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete chainManager;
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    QuickdrawTestStack wireless;
    Player* player;
    MatchManager* matchManager;
    ChainManager* chainManager;
    FakePlatformClock* fakeClock;
};

// ============================================
// Scenario 1: DUT presses button first, then receives result
// ============================================

inline void duelButtonPressTransitionsToDuelPushed(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    EXPECT_FALSE(duelState.transitionToDuelPushed());
    
    // Simulate button press via match manager callback
    suite->fakeClock->advance(200); // 200ms reaction time
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    duelState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(duelState.transitionToDuelPushed());
}

inline void duelButtonPressCalculatesReactionTime(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    // Advance 250ms (simulating reaction time)
    suite->fakeClock->advance(250);
    
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 250);
}

inline void duelButtonPressAppliesMasherPenalty(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    DuelCountdown countdownState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager);
    
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
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(200);
    
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    // Reaction time should be 200ms + (2 * 75ms penalty) = 350ms
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 350);
}

inline void duelButtonPressBroadcastsDrawResult(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    size_t sentBefore = suite->wireless.sent().size();

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);

    suite->fakeClock->advance(200);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);

    ASSERT_GT(suite->wireless.sent().size(), sentBefore);
    EXPECT_EQ(suite->wireless.sent().back().packet.command, QDCommand::DRAW_RESULT);
}

inline void duelPushedWaitsForOpponentResult(DuelStateTests* suite) {
    DuelPushed pushedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    pushedState.onStateMounted(&suite->device);
    
    EXPECT_FALSE(pushedState.transitionToDuelResult());
    
    // Advance less than grace period (900ms)
    suite->fakeClock->advance(500);
    pushedState.onStateLoop(&suite->device);
    
    EXPECT_FALSE(pushedState.transitionToDuelResult());
}

inline void duelPushedTransitionsOnResultReceived(DuelStateTests* suite) {
    DuelPushed pushedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    pushedState.onStateMounted(&suite->device);
    
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 300, false));

    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(pushedState.transitionToDuelResult());
}

// ============================================
// Scenario 2: DUT receives result first, then presses button
// ============================================

inline void duelReceivedResultTransitionsToDuelReceivedResult(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));

    duelState.onStateLoop(&suite->device);

    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
}

inline void duelReceivedResultWaitsForButtonPress(DuelStateTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));

    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    EXPECT_FALSE(receivedState.transitionToDuelResult());
    
    // Advance less than grace period (750ms)
    suite->fakeClock->advance(400);
    receivedState.onStateLoop(&suite->device);

    // Within grace and no button press yet: must still be waiting, not transition.
    EXPECT_FALSE(receivedState.transitionToDuelResult());
}

inline void duelButtonPressDuringGracePeriodTransitions(DuelStateTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));

    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(300);
    
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    
    receivedState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(receivedState.transitionToDuelResult());
}

// ============================================
// Scenario 3: Neither device presses (timeout)
// ============================================

inline void duelTimeoutTransitionsToIdle(DuelStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    EXPECT_FALSE(duelState.transitionToIdle());
    
    // Advance past timeout (4000ms)
    suite->fakeClock->advance(4100);
    duelState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(duelState.transitionToIdle());
}

// ============================================
// Scenario 4: DUT presses, opponent never responds
// ============================================

inline void duelPushedGracePeriodExpiresTransitions(DuelStateTests* suite) {
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    DuelPushed pushedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    pushedState.onStateMounted(&suite->device);

    // Advance past DUEL_RESULT_GRACE_PERIOD (1700ms).
    suite->fakeClock->advance(1800);
    pushedState.onStateLoop(&suite->device);

    EXPECT_TRUE(pushedState.transitionToDuelResult());
}

inline void duelOpponentTimeoutMeansWin(DuelStateTests* suite) {
    suite->player->setIsHunter(true);

    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 0, false));

    EXPECT_TRUE(suite->matchManager->didWin());
}

// ============================================
// Scenario 5: DUT never presses, opponent does
// ============================================

inline void duelGracePeriodExpiresSetsNeverPressed(DuelStateTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));
    
    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    // Advance past grace period (750ms)
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(receivedState.transitionToDuelResult());
}

inline void duelGracePeriodExpiresSendsPityTime(DuelStateTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));
    
    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    // Advance past grace period
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    // Hunter draw time should now be set (pity time)
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_GT(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 0);
}

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
        matchManager->initialize(player, &storage, &wireless.transport);
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
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    QuickdrawTestStack wireless;
    Player* player;
    MatchManager* matchManager;
    FakePlatformClock* fakeClock;
};

inline void resultHunterWinsWithFasterTime(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 300, false));

    EXPECT_TRUE(suite->matchManager->didWin());
}

// A voided duel inside an active shootout must abort the tournament. The
// Idle path is the wrong terminal because neither duelist broadcasts a
// MATCH_RESULT, so the bracket cannot advance.
inline void voidedDuelInShootoutAbortsTournament(DuelResultTests* suite) {
    ChainManager chainManager(suite->player, suite->device.wirelessManager,
                         &suite->device.fakeRemoteDeviceCoordinator);
    ShootoutManager shootout(suite->player, suite->device.wirelessManager,
                             &suite->device.fakeRemoteDeviceCoordinator, &chainManager);

    uint8_t selfMac[6] = {0x01, 0, 0, 0, 0, 0};
    ON_CALL(*suite->device.mockPeerComms, getMacAddress())
        .WillByDefault(testing::Return(selfMac));
    shootout.setLoopMembersForTest({
        {0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}
    });
    shootout.startProposal();
    ASSERT_TRUE(shootout.active());

    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->voidCurrentMatch();
    ASSERT_TRUE(suite->matchManager->isVoided());

    DuelResult resultState(suite->player, suite->matchManager, &shootout);
    resultState.onStateMounted(&suite->device);

    // Wire transitions in populateStateMap order. checkTransitions walks
    // them in registration order and returns the first match; abort must
    // precede eliminated since both predicates fire on a voided duel.
    State shAborted(QuickdrawStateId::SHOOTOUT_ABORTED);
    State shEliminated(QuickdrawStateId::SHOOTOUT_ELIMINATED);
    State idle(QuickdrawStateId::IDLE);
    resultState.addTransition(new StateTransition(
        std::bind(&DuelResult::transitionToShootoutAbortOnVoid, &resultState),
        &shAborted));
    resultState.addTransition(new StateTransition(
        std::bind(&DuelResult::transitionToIdleOnVoid, &resultState),
        &idle));
    resultState.addTransition(new StateTransition(
        std::bind(&DuelResult::transitionToShootoutEliminated, &resultState),
        &shEliminated));

    State* next = resultState.checkTransitions();
    EXPECT_EQ(next, &shAborted);
    EXPECT_EQ(shootout.getPhase(), ShootoutManager::Phase::ABORTED);
}

inline void resultBountyWinsWithFasterTime(DuelResultTests* suite) {
    suite->player->setIsHunter(false);
    // Simulate hunter sending SEND_MATCH_ID; this bounty receives and creates the match.
    // Use the fixture's stubbed RDC peer MAC so the SEND_MATCH_ID gate accepts it.
    uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawPacket sendMatchCmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "test-match-id-000000000000000", "5678", 0, true);
    suite->matchManager->listenForMatchEvents(hunterMac, sendMatchCmd);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(hunterMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, "test-match-id-000000000000000", "5678", 250, true));

    EXPECT_TRUE(suite->matchManager->didWin());
}

inline void resultTiedTimesFavorOpponent(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 250, false));

    // With equal times, hunter_time < bounty_time is false, so hunter loses
    EXPECT_FALSE(suite->matchManager->didWin());
}

inline void resultOpponentTimeoutMeansAutoWin(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 0, false));

    EXPECT_TRUE(suite->matchManager->didWin());
}

inline void resultWinTransitionsToWinState(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 300, false));

    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    resultState.onStateMounted(&suite->device);

    EXPECT_TRUE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

inline void resultLoseTransitionsToLoseState(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 200, false));

    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    resultState.onStateMounted(&suite->device);

    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_TRUE(resultState.transitionToLose());
}

inline void resultPlayerStatsUpdatedOnWin(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    int initialWins = suite->player->getWins();
    int initialStreak = suite->player->getStreak();
    int initialMatches = suite->player->getMatchesPlayed();
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 300, false));

    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    resultState.onStateMounted(&suite->device);

    EXPECT_EQ(suite->player->getWins(), initialWins + 1);
    EXPECT_EQ(suite->player->getStreak(), initialStreak + 1);
    EXPECT_EQ(suite->player->getMatchesPlayed(), initialMatches + 1);
}

inline void resultPlayerStatsUpdatedOnLoss(DuelResultTests* suite) {
    suite->player->setIsHunter(true);
    
    suite->player->incrementWins();
    suite->player->incrementStreak();
    suite->player->incrementStreak();
    
    int initialLosses = suite->player->getLosses();
    int initialMatches = suite->player->getMatchesPlayed();
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 200, false));

    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    resultState.onStateMounted(&suite->device);

    EXPECT_EQ(suite->player->getLosses(), initialLosses + 1);
    EXPECT_EQ(suite->player->getStreak(), 0); // Streak reset
    EXPECT_EQ(suite->player->getMatchesPlayed(), initialMatches + 1);
}

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
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 300, false));

    // Mounting the result state finalizes the match: the storage write/writeUChar
    // EXPECT_CALLs above are the assertion, verified at mock teardown.
    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    resultState.onStateMounted(&suite->device);
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
        matchManager->initialize(player, &storage, &wireless.transport);
        wireFixtureRdcForMatchManager(device, matchManager);

        chainManager = new ChainManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

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
        delete chainManager;
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    QuickdrawTestStack wireless;
    Player* player;
    MatchManager* matchManager;
    ChainManager* chainManager;
    FakePlatformClock* fakeClock;
};

inline void cleanupIdleClearsButtonCallbacks(StateCleanupTests* suite) {
    Idle idleState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    
    idleState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    idleState.onStateDismounted(&suite->device);
}

inline void cleanupCountdownClearsButtonCallbacks(StateCleanupTests* suite) {
    DuelCountdown countdownState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    countdownState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    countdownState.onStateDismounted(&suite->device);
}

inline void cleanupDuelStateDoesNotClearCallbacksOnDismount(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    
    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    
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

inline void cleanupDuelReceivedResultClearsButtonCallbacks(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));

    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    receivedState.onStateDismounted(&suite->device);
}

inline void cleanupDuelStateInvalidatesTimer(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    // Player is hunter, so OUTPUT_JACK must be connected for isConnected() to return true.
    // Without this, onStateLoop would transition to idle due to disconnection, not the timer.
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(2000);
    duelState.onStateLoop(&suite->device);
    
    EXPECT_FALSE(duelState.transitionToIdle());
    
    duelState.onStateDismounted(&suite->device);
    
    EXPECT_FALSE(duelState.transitionToIdle());
    EXPECT_FALSE(duelState.transitionToDuelPushed());
    EXPECT_FALSE(duelState.transitionToDuelReceivedResult());
}

inline void cleanupCountdownStateInvalidatesTimer(StateCleanupTests* suite) {
    DuelCountdown countdownState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    countdownState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(2100);
    countdownState.onStateLoop(&suite->device);
    
    countdownState.onStateDismounted(&suite->device);
    
    EXPECT_FALSE(countdownState.shallWeBattle());
}

inline void cleanupDuelResultClearsWirelessCallbacks(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 300, false));

    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    resultState.onStateMounted(&suite->device);

    resultState.onStateDismounted(&suite->device);
    
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

inline void cleanupMatchManagerClearsCurrentMatch(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    
    suite->matchManager->clearCurrentMatch();
    
    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
}

inline void cleanupMatchManagerClearsDuelState(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    
    suite->matchManager->setDuelLocalStartTime(5000);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 0, false));
    suite->matchManager->setHunterDrawTime(200);

    suite->matchManager->clearCurrentMatch();
    
    EXPECT_EQ(suite->matchManager->getDuelLocalStartTime(), 0);
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
}

// ============================================
// Duel State Callback Cleanup
// ============================================

inline void cleanupDuelStateClearsCallbacksWhenGoingToDuelReceivedResult(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    duelState.onStateMounted(&suite->device);

    // Simulate receiving opponent's result so transitionToDuelReceivedResult fires
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));
    duelState.onStateLoop(&suite->device);
    ASSERT_TRUE(duelState.transitionToDuelReceivedResult());

    // Duel must now remove callbacks on dismount so DuelReceivedResult starts clean
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    duelState.onStateDismounted(&suite->device);
}

inline void pushedClearsMatchOnDisconnect(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setReceivedButtonPush();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    // FakeRemoteDeviceCoordinator always reports DISCONNECTED. The dismount clear
    // is gated on the *debounced* disconnect, so prime the debounce and advance
    // past the window before dismounting; a single-tick blip must not clear.
    DuelPushed pushedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    pushedState.onStateMounted(&suite->device);
    pushedState.disconnectedBackToIdle();  // start the disconnect debounce
    suite->fakeClock->advance(2000);
    pushedState.onStateDismounted(&suite->device);

    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
}

// A single-tick disconnect during the void->DuelResult commit must not clear
// the match: DuelResult still needs the voided/resolved match to persist it for
// server-side dedup. Only a debounced disconnect (back to idle) clears.
inline void pushedDoesNotClearMatchOnTransientDisconnect(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setReceivedButtonPush();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    // FakeRemoteDeviceCoordinator reports DISCONNECTED, but the debounce never
    // elapses (no clock advance), so this is a transient blip, not a persistent
    // loss. The match must survive the dismount.
    DuelPushed pushedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    pushedState.onStateMounted(&suite->device);
    pushedState.onStateDismounted(&suite->device);

    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value());
}

// A single !isConnected() tick from a cable nudge must not abort a duel
// mid-countdown. In a shootout match that would orphan the duelist (Idle's
// auto-trigger to ShootoutProposal is gated on !shootout->active(), false
// mid-match) and leave spectators waiting forever for a MATCH_RESULT that
// will never arrive.
inline void countdownDebouncesTransientDisconnect(StateCleanupTests* suite) {
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    DuelCountdown countdown(suite->player, suite->matchManager,
                            &suite->device.fakeRemoteDeviceCoordinator,
                            suite->chainManager);

    // Single-tick blip: should be absorbed by the debounce.
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    EXPECT_FALSE(countdown.disconnectedBackToIdle());

    // Recovery within the window clears the timer.
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    EXPECT_FALSE(countdown.disconnectedBackToIdle());

    // Persistent loss past the debounce window: now the abort fires.
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    EXPECT_FALSE(countdown.disconnectedBackToIdle());  // start debounce
    suite->fakeClock->advance(2000);
    EXPECT_TRUE(countdown.disconnectedBackToIdle());
}

// Same debounce contract on DuelPushed (between BATTLE press and result
// arrival). A blip here threw away the duel before the grace period could
// resolve it.
inline void duelPushedDebouncesTransientDisconnect(StateCleanupTests* suite) {
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    DuelPushed pushed(suite->matchManager,
                      &suite->device.fakeRemoteDeviceCoordinator);

    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    EXPECT_FALSE(pushed.disconnectedBackToIdle());

    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    EXPECT_FALSE(pushed.disconnectedBackToIdle());

    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    EXPECT_FALSE(pushed.disconnectedBackToIdle());
    suite->fakeClock->advance(2000);
    EXPECT_TRUE(pushed.disconnectedBackToIdle());
}

// Same debounce contract on DuelReceivedResult.
inline void duelReceivedResultDebouncesTransientDisconnect(StateCleanupTests* suite) {
    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    DuelReceivedResult received(suite->matchManager,
                                &suite->device.fakeRemoteDeviceCoordinator);

    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    EXPECT_FALSE(received.disconnectedBackToIdle());

    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    EXPECT_FALSE(received.disconnectedBackToIdle());

    suite->device.fakeRemoteDeviceCoordinator.setPortStatus(
        SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    EXPECT_FALSE(received.disconnectedBackToIdle());
    suite->fakeClock->advance(2000);
    EXPECT_TRUE(received.disconnectedBackToIdle());
}

inline void receivedResultClearsMatchOnDisconnect(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->matchManager->getCurrentMatch()->getMatchId(),
        "boun", 150, false));

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());

    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    receivedState.disconnectedBackToIdle();  // start the disconnect debounce
    suite->fakeClock->advance(2000);
    receivedState.onStateDismounted(&suite->device);

    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
}

// Exercises Quickdraw ctor + dtor under native_asan so leaks in owned
// members (matchManager, chainManager) surface in CI.
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
    }

    void TearDown() override {
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    FakePlatformClock* fakeClock;
    Player* player;
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
};

// Create + destroy many Quickdraw instances; under ASAN (env:native_asan) a
// leak in ~Quickdraw's ownership of matchManager / chainManager would be
// reported. Without ASAN this still catches crashes in the lifecycle path.
inline void quickdrawCtorDtorDoesNotLeak(QuickdrawLifecycleTests* suite) {
    for (int i = 0; i < 5; i++) {
        auto* qd = new Quickdraw(suite->player, &suite->device, nullptr, nullptr, nullptr);
        delete qd;
    }
}

