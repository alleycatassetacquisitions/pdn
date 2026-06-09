#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Existing test headers
#include "state-machine-tests.hpp"
#include "serial-tests.hpp"
#include "device-tests.hpp"

// New test headers
#include "player-tests.hpp"
#include "match-tests.hpp"
#include "utility-tests.hpp"
#include "match-manager-tests.hpp"
#include "integration-tests.hpp"
#include "quickdraw-tests.hpp"
#include "quickdraw-integration-tests.hpp"
#include "direct-peer-table-tests.hpp"
#include "serial-frame-parser-tests.hpp"
#include "rdc-tests.hpp"
#include "chain-manager-tests.hpp"
#include "chain-manager-multi-device-fixture.hpp"
#include "shootout-manager-tests.hpp"
#include "wireless-transport-tests.hpp"
#include "match-manager-concurrent.hpp"
#include "crc16-tests.hpp"
#include "peer-graph-tests.hpp"
#include "peer-graph-codec-tests.hpp"

#if defined(ARDUINO)
#include <Arduino.h>

void setup()
{
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    ::testing::InitGoogleTest();
    // if you plan to use GMock, replace the line above with
    // ::testing::InitGoogleMock();
}

void loop()
{
    if (RUN_ALL_TESTS())
        ;

    delay(1000);
}

#else

// ============================================
// STATE MACHINE TESTS
// ============================================

TEST_F(StateMachineTestSuite, testInitialize) {

    stateMachine->initialize(&stateMachineDevice);

    State* currentState = stateMachine->getCurrentState();

    ASSERT_TRUE(dynamic_cast<InitialTestState*>(currentState)->stateMountedInvoked);
}

TEST_F(StateMachineTestSuite, initializeAddsTransitions) {
    stateMachine->initialize(&stateMachineDevice);

    InitialTestState* initial = dynamic_cast<InitialTestState*>(stateMachine->getStateFromStateMap(0));
    SecondTestState* second = dynamic_cast<SecondTestState*>(stateMachine->getStateFromStateMap(1));
    ThirdTestState* third = dynamic_cast<ThirdTestState*>(stateMachine->getStateFromStateMap(2));
    TerminalTestState* terminal = dynamic_cast<TerminalTestState*>(stateMachine->getStateFromStateMap(3));

    ASSERT_NE(initial->getTransitions().size(), 0);
    ASSERT_NE(second->getTransitions().size(), 0);
    ASSERT_NE(third->getTransitions().size(), 0);
    ASSERT_NE(terminal->getTransitions().size(), 0);

}

TEST_F(StateMachineTestSuite, initializePopulatesStateMap) {
    stateMachine->initialize(&stateMachineDevice);

    std::vector<State *> populatedStates = stateMachine->getStateMap();

    ASSERT_EQ(populatedStates.size(), 4);
}

TEST_F(StateMachineTestSuite, stateMapIsEmptyWhenStateMachineNotInitialized) {

    State* shouldBeNull = stateMachine->getCurrentState();

    ASSERT_EQ(nullptr, shouldBeNull);
}

TEST_F(StateMachineTestSuite, stateShouldTransitionAfterConditionMet) {

    stateMachine->initialize(&stateMachineDevice);

    advanceStateMachineToState(SECOND_STATE);

    State* currentState = stateMachine->getCurrentState();

    ASSERT_TRUE(dynamic_cast<SecondTestState*>(currentState)->stateMountedInvoked);
}

TEST_F(StateMachineTestSuite, whenTransitionIsMetStateDismounts) {

    stateMachine->initialize(&stateMachineDevice);

    advanceStateMachineToState(SECOND_STATE);

    State* initialState = stateMachine->getStateFromStateMap(0);

    ASSERT_TRUE(dynamic_cast<InitialTestState*>(initialState)->stateDismountedInvoked);
}

TEST_F(StateMachineTestSuite, stateMachineTransitionsThroughAllStates) {

    stateMachine->initialize(&stateMachineDevice);

    advanceStateMachineToState(TERMINAL_STATE);

    State* currentState = stateMachine->getCurrentState();
    ASSERT_TRUE(dynamic_cast<TerminalTestState*>(currentState)->stateMountedInvoked);
}

TEST_F(StateMachineTestSuite, stateLifecyclesAreInvoked) {
    stateMachine->initialize(&stateMachineDevice);

    InitialTestState* initial = dynamic_cast<InitialTestState*>(stateMachine->getStateFromStateMap(0));
    SecondTestState* second = dynamic_cast<SecondTestState*>(stateMachine->getStateFromStateMap(1));
    ThirdTestState* third = dynamic_cast<ThirdTestState*>(stateMachine->getStateFromStateMap(2));
    TerminalTestState* terminal = dynamic_cast<TerminalTestState*>(stateMachine->getStateFromStateMap(3));

    advanceStateMachineToState(SECOND_STATE);

    ASSERT_TRUE(initial->stateMountedInvoked);
    ASSERT_EQ(initial->stateLoopInvoked, INITIAL_TRANSITION_THRESHOLD);
    ASSERT_TRUE(initial->stateDismountedInvoked);
    ASSERT_TRUE(second->stateMountedInvoked);

    advanceStateMachineToState(THIRD_STATE);

    ASSERT_EQ(second->stateLoopInvoked, SECOND_TRANSITION_THRESHOLD);
    ASSERT_TRUE(second->stateDismountedInvoked);
    ASSERT_TRUE(third->stateMountedInvoked);

    advanceStateMachineToState(TERMINAL_STATE);

    ASSERT_EQ(third->stateLoopInvoked, THIRD_TRANSITION_THRESHOLD);
    ASSERT_TRUE(third->stateDismountedInvoked);
    ASSERT_TRUE(terminal->stateMountedInvoked);
}

TEST_F(StateMachineTestSuite, stateDoesNotTransitionUntilConditionIsMet) {
    stateMachine->initialize(&stateMachineDevice);

    advanceStateMachineToState(TERMINAL_STATE);

    int numLoopsBeforeTransition = 5;

    testing::InSequence sequence;

    EXPECT_CALL(*stateMachineDevice.mockHaptics, getIntensity())
    .Times(numLoopsBeforeTransition*2)
    .WillRepeatedly(testing::Return(0))
    .RetiresOnSaturation();

    while(numLoopsBeforeTransition--) {
        stateMachine->onStateLoop(&stateMachineDevice);
        ASSERT_TRUE(stateMachine->getCurrentState()->getStateId() == TERMINAL_STATE);
    }

    EXPECT_CALL(*stateMachineDevice.mockHaptics, getIntensity())
    .Times(2)
    .WillRepeatedly(testing::Return(255))
    .RetiresOnSaturation();

    stateMachine->onStateLoop(&stateMachineDevice);

    ASSERT_TRUE(stateMachine->getCurrentState()->getStateId() == SECOND_STATE);
}

TEST_F(StateMachineTestSuite, whenTwoTransitionsAreMetSimultaneouslyThenTheFirstTransitionAddedTriggersFirst) {
    stateMachine->initialize(&stateMachineDevice);

    advanceStateMachineToState(TERMINAL_STATE);

    testing::InSequence sequence;

    EXPECT_CALL(*stateMachineDevice.mockHaptics, getIntensity())
    .Times(2)
    .WillRepeatedly(testing::Return(255));

    stateMachine->onStateLoop(&stateMachineDevice);

    ASSERT_FALSE(stateMachine->getCurrentState()->getStateId() == INITIAL_STATE);
}

TEST_F(StateMachineTestSuite, whenCurrentStateTransitionIsValidCheckTransitionsSetsNewState) {
    stateMachine->initialize(&stateMachineDevice);

    InitialTestState* initial = dynamic_cast<InitialTestState*>(stateMachine->getCurrentState());

    initial->transitionToSecond = true;

    ASSERT_FALSE(stateMachine->getTransitionReadyFlag());
    ASSERT_EQ(stateMachine->getNewState(), nullptr);

    stateMachine->checkStateTransitions();

    ASSERT_TRUE(stateMachine->getTransitionReadyFlag());
    ASSERT_NE(stateMachine->getNewState(), nullptr);
}

TEST_F(StateMachineTestSuite, commitStateExecutesCorrectlyWhenNewStateIsSet) {
    stateMachine->initialize(&stateMachineDevice);

    InitialTestState* initial = dynamic_cast<InitialTestState*>(stateMachine->getCurrentState());

    initial->transitionToSecond = true;

    stateMachine->checkStateTransitions();

    ASSERT_TRUE(stateMachine->getTransitionReadyFlag());
    ASSERT_NE(stateMachine->getNewState(), nullptr);

    stateMachine->commitState(&stateMachineDevice);

    ASSERT_FALSE(stateMachine->getTransitionReadyFlag());
    ASSERT_EQ(stateMachine->getNewState(), nullptr);
    ASSERT_TRUE(stateMachine->getCurrentState()->getStateId() == SECOND_STATE);
}

// ============================================
// DEVICE TESTS - APP PATTERN
// ============================================

TEST_F(DeviceTestSuite, loadAppConfigMountsLaunchApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    ASSERT_EQ(appOne->mountedCount, 1);
    ASSERT_EQ(appOne->loopCount, 0);
}

TEST_F(DeviceTestSuite, loadAppConfigWithMultipleAppsOnlyMountsLaunchApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    config[APP_THREE] = appThree;
    
    device->loadAppConfig(std::move(config), APP_TWO);
    
    ASSERT_EQ(appOne->mountedCount, 0);
    ASSERT_EQ(appTwo->mountedCount, 1);
    ASSERT_EQ(appThree->mountedCount, 0);
}

TEST_F(DeviceTestSuite, loopCallsCurrentAppStateLoop) {
    AppConfig config;
    config[APP_ONE] = appOne;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    device->loop();
    device->loop();
    device->loop();
    
    ASSERT_EQ(appOne->loopCount, 3);
}

TEST_F(DeviceTestSuite, loopHandlesEmptyAppConfig) {
    // No active app: loop() must not crash.
    device->loop();
    device->loop();
    SUCCEED();
}

TEST_F(DeviceTestSuite, setActiveAppSwitchesToNewApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    // Switch to app two
    device->setActiveApp(APP_TWO);
    
    ASSERT_EQ(appOne->dismountedCount, 1);
    ASSERT_EQ(appTwo->mountedCount, 1);
}

TEST_F(DeviceTestSuite, setActiveAppDismountsCurrentApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    ASSERT_EQ(appOne->dismountedCount, 0);
    
    device->setActiveApp(APP_TWO);
    
    ASSERT_EQ(appOne->dismountedCount, 1);
}

TEST_F(DeviceTestSuite, setActiveAppMountsNewApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    device->setActiveApp(APP_TWO);
    
    ASSERT_EQ(appTwo->mountedCount, 1);
}

TEST_F(DeviceTestSuite, setActiveAppRemountsPreviousApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    // Switch to app two
    device->setActiveApp(APP_TWO);
    
    // Switch back to app one
    device->setActiveApp(APP_ONE);
    
    ASSERT_EQ(appOne->mountedCount, 2);    // Initial mount + remount on return
    ASSERT_EQ(appOne->dismountedCount, 1); // Dismounted when switching to app two
}

TEST_F(DeviceTestSuite, setActiveAppLoopCallsNewApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    device->loop();
    device->loop();
    
    ASSERT_EQ(appOne->loopCount, 2);
    ASSERT_EQ(appTwo->loopCount, 0);
    
    device->setActiveApp(APP_TWO);
    
    device->loop();
    device->loop();
    device->loop();
    
    ASSERT_EQ(appOne->loopCount, 2); // No more loops on app one
    ASSERT_EQ(appTwo->loopCount, 3); // App two now getting loops
}

TEST_F(DeviceTestSuite, appLifecycleSequenceCorrect) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    // Load and launch app one
    device->loadAppConfig(std::move(config), APP_ONE);
    ASSERT_EQ(appOne->mountedCount, 1);
    
    // Run some loops
    device->loop();
    device->loop();
    ASSERT_EQ(appOne->loopCount, 2);
    
    // Switch to app two
    device->setActiveApp(APP_TWO);
    ASSERT_EQ(appOne->dismountedCount, 1);
    ASSERT_EQ(appTwo->mountedCount, 1);
    
    // Run some loops on app two
    device->loop();
    ASSERT_EQ(appTwo->loopCount, 1);
    
    // Switch back to app one
    device->setActiveApp(APP_ONE);
    ASSERT_EQ(appTwo->dismountedCount, 1);
    ASSERT_EQ(appOne->mountedCount, 2);
    
    // Continue loops on app one
    device->loop();
    ASSERT_EQ(appOne->loopCount, 3);
}

TEST_F(DeviceTestSuite, multipleAppSwitchesWorkCorrectly) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    config[APP_THREE] = appThree;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    // APP_ONE -> APP_TWO
    device->setActiveApp(APP_TWO);
    ASSERT_EQ(appTwo->mountedCount, 1);
    
    // APP_TWO -> APP_THREE
    device->setActiveApp(APP_THREE);
    ASSERT_EQ(appThree->mountedCount, 1);
    
    // APP_THREE -> APP_ONE (remount)
    device->setActiveApp(APP_ONE);
    ASSERT_EQ(appOne->mountedCount, 2);
    
    // APP_ONE -> APP_TWO (remount)
    device->setActiveApp(APP_TWO);
    ASSERT_EQ(appTwo->mountedCount, 2);
}

TEST_F(DeviceTestSuite, inactiveAppLoopCountUnchanged) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    device->loop();
    device->loop();
    device->loop();
    int loopsBeforeSwitch = appOne->loopCount;
    
    // Switch away
    device->setActiveApp(APP_TWO);
    
    // Run loops on app two — app one should not receive any
    device->loop();
    device->loop();
    
    ASSERT_EQ(appOne->loopCount, loopsBeforeSwitch);
    
    // Switch back and continue
    device->setActiveApp(APP_ONE);
    device->loop();
    
    ASSERT_EQ(appOne->loopCount, loopsBeforeSwitch + 1);
}

TEST_F(DeviceTestSuite, loopExecutesDriversBeforeAppLoop) {
    // This test verifies that Device::loop() calls both
    // driverManager.execDrivers() and the app's onStateLoop
    AppConfig config;
    config[APP_ONE] = appOne;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    // Device loop should execute drivers and then app loop
    device->loop();
    
    // App loop was called
    ASSERT_EQ(appOne->loopCount, 1);
    
    // Note: We can't directly test execDrivers() was called since
    // it's not mockable, but this documents the expected behavior
}

// ============================================
// SERIAL TESTS
// ============================================

TEST(NativeSerialDriver, byteCallbackReceivesArbitraryBytes) {
    nativeSerialDriverByteCallbackReceivesArbitraryBytes();
}

TEST(NativeSerialDriver, byteCallbackFragmentsWhenEnabled) {
    nativeSerialDriverByteCallbackFragmentsWhenEnabled();
}

// ============================================
// PLAYER TESTS
// ============================================

TEST_F(PlayerTestSuite, jsonRoundTripPreservesAllFields) {
    playerJsonRoundTripPreservesAllFields(player);
}

TEST_F(PlayerTestSuite, allegianceFromIntSetsCorrectly) {
    playerAllegianceFromIntSetsCorrectly(player);
}

TEST_F(PlayerTestSuite, allegianceFromStringSetsCorrectly) {
    playerAllegianceFromStringSetsCorrectly(player);
}

TEST_F(PlayerTestSuite, reactionTimeAverageCalculatesCorrectly) {
    playerReactionTimeAverageCalculatesCorrectly(player);
}

// ============================================
// MATCH TESTS
// ============================================

TEST_F(MatchTestSuite, jsonRoundTripPreservesAllFields) {
    matchJsonRoundTripPreservesAllFields();
}

TEST_F(MatchTestSuite, jsonContainsWinnerFlag) {
    matchJsonContainsWinnerFlag();
}

TEST_F(MatchTestSuite, setupClearsDrawTimes) {
    matchSetupClearsDrawTimes();
}

TEST_F(MatchTestSuite, drawTimesSetCorrectly) {
    matchDrawTimesSetCorrectly();
}

TEST_F(MatchTestSuite, withZeroDrawTimes) {
    matchWithZeroDrawTimes();
}

// ============================================
// MAC ADDRESS TESTS
// ============================================

TEST_F(MACTestSuite, macToStringProducesCorrectFormat) {
    macToStringProducesCorrectFormat();
}

TEST_F(MACTestSuite, macToStringHandlesZeros) {
    macToStringHandlesZeros();
}

TEST_F(MACTestSuite, stringToMacParsesValidFormat) {
    stringToMacParsesValidFormat();
}

TEST_F(MACTestSuite, stringToMacRejectsInvalidLength) {
    stringToMacRejectsInvalidLength();
}

TEST_F(MACTestSuite, macToUInt64ProducesCorrectValue) {
    macToUInt64ProducesCorrectValue();
}

TEST_F(MACTestSuite, macRoundTripPreservesData) {
    macRoundTripPreservesData();
}

// ============================================
// TIMER TESTS
// ============================================

TEST_F(TimerTestSuite, expiresAfterDuration) {
    timerExpiresAfterDuration(fakeClock);
}

TEST_F(TimerTestSuite, doesNotExpireBeforeDuration) {
    timerDoesNotExpireBeforeDuration(fakeClock);
}

TEST_F(TimerTestSuite, invalidateStopsTimer) {
    timerInvalidateStopsTimer(fakeClock);
}

TEST_F(TimerTestSuite, elapsedTimeIsAccurate) {
    timerElapsedTimeIsAccurate(fakeClock);
}

TEST_F(TimerTestSuite, withNullClockHandlesGracefully) {
    timerWithNullClockHandlesGracefully();
}

// ============================================
// MATCH MANAGER TESTS
// ============================================

TEST_F(MatchManagerTestSuite, clearCancelsInflightWithoutAbandon) {
    matchManagerClearCancelsInflightWithoutAbandon(this);
}

TEST_F(MatchManagerTestSuite, boostSubtractedFromHunterReactionTime) {
    matchManagerBoostSubtractedFromHunterReactionTime(this);
}

TEST_F(MatchManagerTestSuite, shootoutMatchExcludedFromReactionStats) {
    matchManagerShootoutMatchExcludedFromReactionStats(this);
}

TEST_F(MatchManagerTestSuite, shootoutMatchIgnoresChainBoost) {
    matchManagerShootoutMatchIgnoresChainBoost(this);
}
TEST_F(MatchManagerTestSuite, shootoutMatchRoleDecoupledFromGlobalRole) {
    matchManagerShootoutMatchRoleDecoupledFromGlobalRole(this);
}

TEST_F(MatchManagerTestSuite, initializeCreatesMatch) {
    matchManagerInitializeCreatesMatch(matchManager, player);
}

TEST_F(MatchManagerTestSuite, initializePreventsDoubleActive) {
    matchManagerInitializePreventsDoubleActive(matchManager, player);
}

TEST_F(MatchManagerTestSuite, bountyReceivesMatchViaHandshake) {
    matchManagerBountyReceivesMatchViaHandshake(matchManager, player);
}

TEST_F(MatchManagerTestSuite, hunterWinsWhenFaster) {
    matchManagerHunterWinsWhenFaster(matchManager, player);
}

TEST_F(MatchManagerTestSuite, hunterLosesWhenSlower) {
    matchManagerHunterLosesWhenSlower(matchManager, player);
}

TEST_F(MatchManagerTestSuite, bountyWinsWhenFaster) {
    matchManagerBountyWinsWhenFaster(matchManager, player);
}

TEST_F(MatchManagerTestSuite, bountyLosesWhenSlower) {
    matchManagerBountyLosesWhenSlower(matchManager, player);
}

TEST_F(MatchManagerTestSuite, hunterWinsWhenBountyNeverPressed) {
    matchManagerHunterWinsWhenBountyNeverPressed(matchManager, player);
}

TEST_F(MatchManagerTestSuite, bountyWinsWhenHunterNeverPressed) {
    matchManagerBountyWinsWhenHunterNeverPressed(matchManager, player);
}

TEST_F(MatchManagerTestSuite, opponentResultIsFirstWriterWins) {
    matchManagerOpponentResultIsFirstWriterWins(matchManager, player);
}

TEST_F(MatchManagerTestSuite, lateTimeoutDoesNotClobberWinningTime) {
    matchManagerLateTimeoutDoesNotClobberWinningTime(matchManager, player);
}

TEST_F(MatchManagerTestSuite, tracksDuelState) {
    matchManagerTracksDuelState(matchManager, player);
}

TEST_F(MatchManagerTestSuite, graceExpiredAloneFinalizes) {
    matchManagerGraceExpiredAloneFinalizes(matchManager, player);
}

TEST_F(MatchManagerTestSuite, voidedDuelPersistsWithFlag) {
    matchManagerVoidedDuelPersistsWithFlag(matchManager, player);
}

TEST_F(MatchManagerTestSuite, neverPressedAbandonPreservesLoss) {
    matchManagerNeverPressedAbandonPreservesLoss(matchManager, player);
}

TEST_F(MatchManagerTestSuite, drawResultAbandonVoids) {
    matchManagerDrawResultAbandonVoids(matchManager, player);
}

TEST_F(MatchManagerTestSuite, staleAbandonForOtherPeerIgnored) {
    matchManagerStaleAbandonForOtherPeerIgnored(matchManager, player);
}

TEST_F(MatchManagerTestSuite, setupAbandonClearsUnreadyMatch) {
    matchManagerSetupAbandonClearsUnreadyMatch(matchManager, player);
}

TEST_F(MatchManagerTestSuite, rejectsNeverPressedFromStranger) {
    matchManagerRejectsNeverPressedFromStranger(matchManager, player);
}

TEST_F(MatchManagerTestSuite, rejectsWrongMatchId) {
    matchManagerRejectsWrongMatchId(matchManager, player);
}

TEST_F(MatchManagerTestSuite, acceptsResultFromOpponent) {
    matchManagerAcceptsResultFromOpponent(matchManager, player);
}

TEST_F(MatchManagerTestSuite, sendMatchIdRejectsUnknownPeer) {
    matchManagerSendMatchIdRejectsUnknownPeer(matchManager, player);
}

TEST_F(MatchManagerTestSuite, gracePeriodPath) {
    matchManagerGracePeriodPath(matchManager, player);
}

TEST_F(MatchManagerTestSuite, clearMatchResetsState) {
    matchManagerClearMatchResetsState(matchManager, player);
}

TEST_F(MatchManagerTestSuite, setDrawTimesRequiresActiveMatch) {
    matchManagerSetDrawTimesRequiresActiveMatch(matchManager, player);
}

TEST_F(MatchManagerTestSuite, duelStartTimeTracking) {
    matchManagerDuelStartTimeTracking(matchManager, player);
}

TEST_F(MatchManagerTestSuite, clearCurrentMatchResetsMasherCount) {
    matchManagerClearCurrentMatchResetsMasherCount(this);
}

TEST_F(MatchManagerTestSuite, matchIsReadyFalseBeforeHandshake) {
    matchManagerMatchIsReadyFalseBeforeHandshake(matchManager, player);
}

TEST_F(MatchManagerTestSuite, hunterMatchIsReadyAfterAck) {
    matchManagerHunterMatchIsReadyAfterAck(matchManager, player);
}

TEST_F(MatchManagerTestSuite, bountyMatchIsReadyAfterReceivingMatch) {
    matchManagerBountyMatchIsReadyAfterReceivingMatch(matchManager, player);
}

TEST_F(MatchManagerTestSuite, clearMatchResetsMatchIsReadyFlag) {
    matchManagerClearMatchResetsMatchIsReadyFlag(matchManager, player);
}

TEST_F(MatchManagerTestSuite, roleMismatchClearsInitiatorMatch) {
    matchManagerRoleMismatchClearsInitiatorMatch(matchManager, player);
}

TEST(MatchManagerConcurrent, driverExecSerializesMatchManagerAccess) {
    matchManagerConcurrentDriverVsReader();
}

// ============================================
// INTEGRATION TESTS
// ============================================

TEST_F(DuelIntegrationTestSuite, completeDuelFlowHunterWins) {
    completeDuelFlowHunterWins(this);
}

TEST_F(DuelIntegrationTestSuite, completeDuelFlowBountyWins) {
    completeDuelFlowBountyWins(this);
}

TEST_F(DuelIntegrationTestSuite, duelWithTiedReactionTimes) {
    duelWithTiedReactionTimes(this);
}

TEST_F(DuelIntegrationTestSuite, duelWithOpponentTimeout) {
    duelWithOpponentTimeout(this);
}

// ============================================
// QUICKDRAW STATE TESTS - IDLE
// ============================================

TEST_F(IdleStateTests, mountRegistersButtonCallbacks) {
    idleMountRegistersButtonCallbacks(this);
}

TEST_F(IdleStateTests, doesNotTransitionWhenDisconnected) {
    idleDoesNotTransitionWhenDisconnected(this);
}

TEST_F(IdleStateTests, stateClearsOnDismount) {
    idleStateClearsOnDismount(this);
}

TEST_F(IdleStateTests, doesNotTransitionWithMatchButNotReady) {
    idleDoesNotTransitionWithMatchButNotReady(this);
}

TEST_F(IdleStateTests, transitionsToDuelCountdownWhenMatchIsReady) {
    idleTransitionsToDuelCountdownWhenMatchIsReady(this);
}
TEST_F(IdleStateTests, keepsReadyMatchAcrossReinit) {
    idleKeepsReadyMatchAcrossReinit(this);
}

// ============================================
// PEER GRAPH TESTS
// ============================================

TEST_F(PeerGraphTests, storesBeaconBySource) {
    peerGraphStoresBeaconBySource(this);
}

TEST_F(PeerGraphTests, mutualEdgeRequiresBothClaims) {
    peerGraphMutualEdgeRequiresBothClaims(this);
}

TEST_F(PeerGraphTests, isInLoopThreeNodeRing) {
    peerGraphIsInLoopThreeNodeRing(this);
}

TEST_F(PeerGraphTests, threeNodeChainNotInLoop) {
    peerGraphThreeNodeChainNotInLoop(this);
}

TEST_F(PeerGraphTests, fourNodeRingInLoop) {
    peerGraphFourNodeRingInLoop(this);
}

TEST_F(PeerGraphTests, selfIslandNotInLoop) {
    peerGraphSelfIslandNotInLoop(this);
}

TEST_F(PeerGraphTests, rejectsPoisonBeaconSource) {
    peerGraphRejectsPoisonBeaconSource(this);
}

TEST_F(PeerGraphTests, topologyStableAfterDebounceWindow) {
    peerGraphTopologyStableAfterDebounceWindow(this);
}
TEST_F(PeerGraphTests, backwardsClockNotStable) {
    peerGraphBackwardsClockNotStable(this);
}

TEST_F(PeerGraphTests, unchangedBeaconDoesNotResetStability) {
    peerGraphUnchangedBeaconDoesNotResetStability(this);
}

TEST_F(PeerGraphTests, roleFlipDoesNotResetStability) {
    peerGraphRoleFlipDoesNotResetStability(this);
}

TEST_F(PeerGraphTests, halfOpenBeaconDoesNotResetStability) {
    peerGraphHalfOpenBeaconDoesNotResetStability(this);
}

TEST_F(PeerGraphTests, nonMutualNodeExcludedFromMembers) {
    peerGraphNonMutualNodeExcludedFromMembers(this);
}

TEST_F(PeerGraphTests, peerDropsOutWhenSelfStopsClaiming) {
    peerGraphPeerDropsOutWhenSelfStopsClaiming(this);
}

TEST_F(PeerGraphTests, twoDeviceBothJacksNotLoopButRetainsPeer) {
    peerGraphTwoDeviceBothJacksNotLoopButRetainsPeer(this);
}

TEST_F(PeerGraphTests, countReachableSplitsChainAtSelf) {
    peerGraphCountReachableSplitsChainAtSelf(this);
}

TEST_F(PeerGraphTests, countReachableDirectPeerBeforeBeacon) {
    peerGraphCountReachableDirectPeerBeforeBeacon(this);
}

TEST_F(PeerGraphTests, countReachableZeroForAbsentPeer) {
    peerGraphCountReachableZeroForAbsentPeer(this);
}

TEST_F(ChainManagerTests, getChainLengthChampionOnly) {
    chainGetChainLengthChampionOnly(this);
}

TEST(PeerGraphCodecTests, helloRoundTrip) { codecHelloRoundTrip(); }
TEST(PeerGraphCodecTests, beaconRoundTrip) { codecBeaconRoundTrip(); }
TEST(PeerGraphCodecTests, beaconRoleByteIsOpaque) { codecBeaconRoleByteIsOpaque(); }
TEST(PeerGraphCodecTests, beaconEmptyPeersRoundTrip) { codecBeaconEmptyPeersRoundTrip(); }
TEST(PeerGraphCodecTests, rejectsWrongLength) { codecRejectsWrongLength(); }

TEST(ChainRoleWalkTests, loneHunterIsSelfChampion) { chainRoleLoneHunterIsSelfChampion(); }
TEST(ChainRoleWalkTests, championHunterFacingBounty) { chainRoleChampionHunterFacingBounty(); }
TEST(ChainRoleWalkTests, championWalkToHead) { chainRoleChampionWalkToHead(); }
TEST(ChainRoleWalkTests, championNoneInClosedRing) { chainRoleChampionNoneInClosedRing(); }
TEST(ChainRoleWalkTests, championResolvesInHalfOpenRing) { chainRoleChampionResolvesInHalfOpenRing(); }
TEST(ChainRoleWalkTests, noneSelfHasNoChampion) { chainRoleNoneSelfHasNoChampion(); }
TEST(ChainRoleWalkTests, noneMidChainSeversChain) { chainRoleNoneMidChainSeversChain(); }
TEST(ChainRoleWalkTests, mutualOpponentNoneIsNotAnOpponent) { chainRoleMutualOpponentNoneIsNotAnOpponent(); }

// ============================================
// QUICKDRAW STATE TESTS - COUNTDOWN
// ============================================

TEST_F(DuelCountdownTests, progressesThroughStages) {
    countdownProgressesThroughStages(this);
}

TEST_F(DuelCountdownTests, cleansUpOnDismount) {
    countdownCleansUpOnDismount(this);
}

// ============================================
// QUICKDRAW STATE TESTS - DUEL SCENARIOS
// ============================================

// Scenario 1: DUT presses button first
TEST_F(DuelStateTests, buttonPressTransitionsToDuelPushed) {
    duelButtonPressTransitionsToDuelPushed(this);
}

TEST_F(DuelStateTests, buttonPressCalculatesReactionTime) {
    duelButtonPressCalculatesReactionTime(this);
}

TEST_F(DuelStateTests, buttonPressAppliesMasherPenalty) {
    duelButtonPressAppliesMasherPenalty(this);
}

TEST_F(DuelStateTests, buttonPressBroadcastsDrawResult) {
    duelButtonPressBroadcastsDrawResult(this);
}

TEST_F(DuelStateTests, pushedWaitsForOpponentResult) {
    duelPushedWaitsForOpponentResult(this);
}

TEST_F(DuelStateTests, pushedTransitionsOnResultReceived) {
    duelPushedTransitionsOnResultReceived(this);
}

// Scenario 2: DUT receives result first
TEST_F(DuelStateTests, receivedResultTransitionsToDuelReceivedResult) {
    duelReceivedResultTransitionsToDuelReceivedResult(this);
}

TEST_F(DuelStateTests, receivedResultWaitsForButtonPress) {
    duelReceivedResultWaitsForButtonPress(this);
}

TEST_F(DuelStateTests, buttonPressDuringGracePeriodTransitions) {
    duelButtonPressDuringGracePeriodTransitions(this);
}

// Scenario 3: Neither presses (timeout)
TEST_F(DuelStateTests, timeoutTransitionsToIdle) {
    duelTimeoutTransitionsToIdle(this);
}

// Scenario 4: DUT presses, opponent never responds
TEST_F(DuelStateTests, pushedGracePeriodExpiresTransitions) {
    duelPushedGracePeriodExpiresTransitions(this);
}

TEST_F(DuelStateTests, opponentTimeoutMeansWin) {
    duelOpponentTimeoutMeansWin(this);
}

// Scenario 5: DUT never presses, opponent does
TEST_F(DuelStateTests, gracePeriodExpiresSetsNeverPressed) {
    duelGracePeriodExpiresSetsNeverPressed(this);
}

TEST_F(DuelStateTests, gracePeriodExpiresSendsPityTime) {
    duelGracePeriodExpiresSendsPityTime(this);
}

TEST_F(DuelStateTests, neverPressedMeansLose) {
    duelNeverPressedMeansLose(this);
}

// ============================================
// QUICKDRAW STATE TESTS - DUEL RESULT
// ============================================

TEST_F(DuelResultTests, hunterWinsWithFasterTime) {
    resultHunterWinsWithFasterTime(this);
}

TEST_F(DuelResultTests, voidedDuelInShootoutAbortsTournament) {
    voidedDuelInShootoutAbortsTournament(this);
}

TEST_F(DuelResultTests, bountyWinsWithFasterTime) {
    resultBountyWinsWithFasterTime(this);
}

TEST_F(DuelResultTests, tiedTimesFavorOpponent) {
    resultTiedTimesFavorOpponent(this);
}

TEST_F(DuelResultTests, opponentTimeoutMeansAutoWin) {
    resultOpponentTimeoutMeansAutoWin(this);
}

TEST_F(DuelResultTests, winTransitionsToWinState) {
    resultWinTransitionsToWinState(this);
}

TEST_F(DuelResultTests, loseTransitionsToLoseState) {
    resultLoseTransitionsToLoseState(this);
}

TEST_F(DuelResultTests, playerStatsUpdatedOnWin) {
    resultPlayerStatsUpdatedOnWin(this);
}

TEST_F(DuelResultTests, playerStatsUpdatedOnLoss) {
    resultPlayerStatsUpdatedOnLoss(this);
}

TEST_F(DuelResultTests, matchFinalizedOnResult) {
    resultMatchFinalizedOnResult(this);
}

// ============================================
// QUICKDRAW STATE TESTS - STATE CLEANUP
// ============================================

TEST_F(StateCleanupTests, idleClearsButtonCallbacks) {
    cleanupIdleClearsButtonCallbacks(this);
}

TEST_F(StateCleanupTests, countdownClearsButtonCallbacks) {
    cleanupCountdownClearsButtonCallbacks(this);
}

TEST_F(StateCleanupTests, duelStateDoesNotClearCallbacksOnDismount) {
    cleanupDuelStateDoesNotClearCallbacksOnDismount(this);
}

TEST_F(StateCleanupTests, duelReceivedResultClearsButtonCallbacks) {
    cleanupDuelReceivedResultClearsButtonCallbacks(this);
}

TEST_F(StateCleanupTests, duelStateInvalidatesTimer) {
    cleanupDuelStateInvalidatesTimer(this);
}

TEST_F(StateCleanupTests, countdownStateInvalidatesTimer) {
    cleanupCountdownStateInvalidatesTimer(this);
}

TEST_F(StateCleanupTests, duelResultClearsWirelessCallbacks) {
    cleanupDuelResultClearsWirelessCallbacks(this);
}

TEST_F(StateCleanupTests, matchManagerClearsCurrentMatch) {
    cleanupMatchManagerClearsCurrentMatch(this);
}

TEST_F(StateCleanupTests, matchManagerClearsDuelState) {
    cleanupMatchManagerClearsDuelState(this);
}

TEST_F(StateCleanupTests, duelStateClearsCallbacksWhenGoingToDuelReceivedResult) {
    cleanupDuelStateClearsCallbacksWhenGoingToDuelReceivedResult(this);
}

TEST_F(StateCleanupTests, pushedClearsMatchOnDisconnect) {
    pushedClearsMatchOnDisconnect(this);
}

TEST_F(StateCleanupTests, receivedResultClearsMatchOnDisconnect) {
    receivedResultClearsMatchOnDisconnect(this);
}

TEST_F(StateCleanupTests, countdownDebouncesTransientDisconnect) {
    countdownDebouncesTransientDisconnect(this);
}

TEST_F(StateCleanupTests, pushedDoesNotClearMatchOnTransientDisconnect) {
    pushedDoesNotClearMatchOnTransientDisconnect(this);
}

TEST_F(StateCleanupTests, duelPushedDebouncesTransientDisconnect) {
    duelPushedDebouncesTransientDisconnect(this);
}

TEST_F(StateCleanupTests, duelReceivedResultDebouncesTransientDisconnect) {
    duelReceivedResultDebouncesTransientDisconnect(this);
}

TEST_F(QuickdrawLifecycleTests, ctorDtorDoesNotLeak) {
    quickdrawCtorDtorDoesNotLeak(this);
}

// ============================================
// QUICKDRAW INTEGRATION TESTS - PACKET PARSING
// ============================================

TEST_F(PacketParsingTests, rejectsMalformedPacket) {
    packetParsingRejectsMalformedPacket(this);
}

TEST_F(PacketParsingTests, dedupsResentReliablePacket) {
    packetParsingDedupsResentReliablePacket(this);
}

TEST_F(PacketParsingTests, listenForMatchResultsSetsOpponentTimeHunter) {
    listenForMatchResultsSetsOpponentTimeHunter(this);
}

TEST_F(PacketParsingTests, listenForMatchResultsSetsOpponentTimeBounty) {
    listenForMatchResultsSetsOpponentTimeBounty(this);
}

TEST_F(PacketParsingTests, listenForMatchResultsHandlesNeverPressed) {
    listenForMatchResultsHandlesNeverPressed(this);
}

TEST_F(PacketParsingTests, listenForMatchResultsIgnoresUnexpectedCommands) {
    listenForMatchResultsIgnoresUnexpectedCommands(this);
}

// ============================================
// QUICKDRAW INTEGRATION TESTS - CALLBACK CHAIN
// ============================================

TEST_F(CallbackChainTests, packetToStateFlag) {
    callbackChainPacketToStateFlag(this);
}

TEST_F(CallbackChainTests, buttonPressCalculatesTime) {
    callbackChainButtonPressCalculatesTime(this);
}

TEST_F(CallbackChainTests, buttonMasherPenalty) {
    callbackChainButtonMasherPenalty(this);
}

TEST_F(CallbackChainTests, buttonPressBroadcasts) {
    callbackChainButtonPressBroadcasts(this);
}

// ============================================
// QUICKDRAW INTEGRATION TESTS - STATE FLOW
// ============================================

TEST_F(StateFlowIntegrationTests, dutPressesFirstWins) {
    stateFlowDutPressesFirstWins(this);
}

TEST_F(StateFlowIntegrationTests, dutReceivesFirstLoses) {
    stateFlowDutReceivesFirstLoses(this);
}
TEST_F(StateFlowIntegrationTests, pressAtGraceExpiryStillCounts) {
    stateFlowPressAtGraceExpiryStillCounts(this);
}

TEST_F(StateFlowIntegrationTests, dutNeverPressesLoses) {
    stateFlowDutNeverPressesLoses(this);
}

TEST_F(StateFlowIntegrationTests, opponentNeverRespondsWins) {
    stateFlowOpponentNeverRespondsWins(this);
}

TEST_F(StateFlowIntegrationTests, throughDuelResultToWin) {
    stateFlowThroughDuelResultToWin(this);
}

TEST_F(StateFlowIntegrationTests, throughDuelResultToLose) {
    stateFlowThroughDuelResultToLose(this);
}

TEST_F(StateFlowIntegrationTests, shootoutMatchWritesNoLifetimeStats) {
    stateFlowShootoutMatchWritesNoLifetimeStats(this);
}

// ============================================
// QUICKDRAW INTEGRATION TESTS - TWO DEVICE SIMULATION
// ============================================

TEST_F(TwoDeviceSimulationTests, hunterPressesFirstBothAgree) {
    twoDeviceHunterPressesFirstBothAgree(this);
}

TEST_F(TwoDeviceSimulationTests, bountyPressesFirstBothAgree) {
    twoDeviceBountyPressesFirstBothAgree(this);
}

TEST_F(TwoDeviceSimulationTests, closeRaceCorrectWinner) {
    twoDeviceCloseRaceCorrectWinner(this);
}

TEST_F(DirectPeerTableTests, getMacPeerReturnsNullWhenNotSet) {
    directPeerTableGetMacPeerReturnsNullWhenNotSet(this);
}

TEST_F(DirectPeerTableTests, getMacPeerReturnsCorrectMac) {
    directPeerTableGetMacPeerReturnsCorrectMac(this);
}

TEST_F(DirectPeerTableTests, removeMacPeerClearsEntry) {
    directPeerTableRemoveMacPeerClearsEntry(this);
}

// ============================================
// SERIAL FRAME PARSER TESTS
// ============================================

TEST_F(SerialFrameParserTests, validBinaryFrameRoutesToFrameHandler) {
    serialFrameParserValidBinaryFrameRoutesToFrameHandler(this);
}

TEST_F(SerialFrameParserTests, badCrcDropsFrame) {
    serialFrameParserBadCrcDropsFrame(this);
}

TEST_F(SerialFrameParserTests, unknownOpcodeDrops) {
    serialFrameParserUnknownOpcodeDrops(this);
}

TEST_F(SerialFrameParserTests, fragmentedFrameAssembles) {
    serialFrameParserFragmentedFrameAssembles(this);
}

TEST_F(SerialFrameParserTests, midFrameTimeoutResets) {
    serialFrameParserMidFrameTimeoutResets(this);
}

TEST_F(SerialFrameParserTests, garbageThenValidFrameResyncs) {
    serialFrameParserGarbageThenValidFrameResyncs(this);
}

TEST_F(SerialFrameParserTests, doubleAAResyncsToPreamble) {
    serialFrameParserDoubleAAResyncsToPreamble(this);
}

TEST_F(SerialFrameParserTests, splitPreambleAcrossFeeds) {
    serialFrameParserSplitPreambleAcrossFeeds(this);
}

// ============================================
// REMOTE DEVICE COORDINATOR TESTS
// ============================================

TEST_F(RDCTests, defaultStateIsDisconnected) { rdcDefaultStateIsDisconnected(this); }
TEST_F(RDCTests, helloSetsMacPeerAndConnected) { rdcHelloSetsMacPeerAndConnected(this); }
TEST_F(RDCTests, helloFromSelfRejected) { rdcHelloFromSelfRejected(this); }
TEST_F(RDCTests, silentLinkClearsPeerAfterThreshold) { rdcSilentLinkClearsPeerAfterThreshold(this); }
TEST_F(RDCTests, silentLinkReleasesEspNowPeerSlot) { rdcSilentLinkReleasesEspNowPeerSlot(this); }
TEST_F(RDCTests, silentLinkSurvivesRefreshWithinWindow) { rdcSilentLinkSurvivesRefreshWithinWindow(this); }
TEST_F(RDCTests, silentLinkFiresDisconnectCallback) { rdcSilentLinkFiresDisconnectCallback(this); }
TEST_F(RDCTests, connectFiresConnectCallback) { rdcConnectFiresConnectCallback(this); }
TEST_F(RDCTests, disconnectCallbackCarriesDeviceType) { rdcDisconnectCallbackCarriesDeviceType(this); }
TEST_F(RDCTests, repeatedHelloFiresConnectOnce) { rdcRepeatedHelloFiresConnectOnce(this); }
TEST_F(RDCTests, isDirectPeerTrueForCableNeighbor) { rdcIsDirectPeerTrueForCableNeighbor(this); }
TEST_F(RDCTests, macPeerChangeEmitsBeacon) { rdcMacPeerChangeEmitsBeacon(this); }
TEST_F(RDCTests, beaconsFormRingIsInLoop) { rdcBeaconsFormRingIsInLoop(this); }
TEST_F(RDCTests, selfSourcedBeaconNotForwarded) { rdcSelfSourcedBeaconNotForwarded(this); }
TEST_F(RDCTests, foreignBeaconFloodedOnOppositeJack) { rdcForeignBeaconFloodedOnOppositeJack(this); }
TEST_F(RDCTests, newPeerReplaysCachedTopology) { rdcNewPeerReplaysCachedTopology(this); }
TEST_F(RDCTests, backstopReplaysChainMemberBeacons) { rdcBackstopReplaysChainMemberBeacons(this); }
TEST_F(RDCTests, beaconReplayIsPaced) { rdcBeaconReplayIsPaced(this); }
TEST_F(RDCTests, sameJackPeerSwapFiresDisconnectThenConnect) { rdcSameJackPeerSwapFiresDisconnectThenConnect(this); }
TEST_F(RDCTests, roleFlipEmitsBeaconImmediately) { rdcRoleFlipEmitsBeaconImmediately(this); }
TEST_F(RDCTests, recvQueueBoundedDropsOldest) { rdcRecvQueueBoundedDropsOldest(this); }


TEST_F(ChainManagerTests, roleDerivationWithChampionTopology) {
    chainRoleDerivationWithChampionTopology(this);
}

TEST_F(ChainManagerTests, canInitiateMatchFalseForBounty) {
    chainCanInitiateMatchFalseForBounty(this);
}

TEST_F(ChainManagerTests, canInitiateMatchFalseWhenInLoop) {
    chainCanInitiateMatchFalseWhenInLoop(this);
}

TEST_F(ChainManagerTests, confirmLifecycle) {
    chainConfirmLifecycle(this);
}

TEST_F(ChainManagerTests, onChainStateChangedClearsOnDrain) {
    chainOnChainStateChangedClearsOnDrain(this);
}

TEST_F(ChainManagerTests, isChampionFalseWithSameRoleOpponent) {
    chainIsChampionFalseWithSameRoleOpponent(this);
}

TEST_F(ChainManagerTests, DirectPeerConnectDoesNotClaim) { chainDirectPeerConnectDoesNotClaim(this); }
TEST_F(ChainManagerTests, coordinatorGuardSuppressesSupporter) {
    chainCoordinatorGuardSuppressesSupporter(this);
}
TEST_F(ChainManagerTests, sendConfirmTargetsChampionMac) {
    chainSendConfirmTargetsChampionMac(this);
}

TEST_F(ChainManagerTests, sendConfirmIncrementsSeqId) {
    chainSendConfirmIncrementsSeqId(this);
}

TEST_F(ChainManagerTests, sendConfirmNoopWhenChampionMacInvalid) {
    chainSendConfirmNoopWhenChampionMacInvalid(this);
}

TEST_F(ChainManagerTests, retryStatsRecordsLifecycle) {
    chainRetryStatsRecordsLifecycle(this);
}

TEST_F(ChainManagerTests, gameEventAckFromWrongMacIgnored) {
    chainGameEventAckFromWrongMacIgnored(this);
}

TEST_F(ChainManagerTests, gameEventCountdownIsTrackedAndRetried) {
    chainGameEventCountdownIsTrackedAndRetried(this);
}

TEST_F(ChainManagerTests, countdownReachesMultiHopSupporter) {
    chainCountdownReachesMultiHopSupporter(this);
}

TEST_F(ChainManagerTests, gameEventWinIsTrackedAndRetried) {
    chainGameEventWinIsTrackedAndRetried(this);
}

TEST_F(ChainManagerTests, gameEventAckClearsPending) {
    chainGameEventAckClearsPending(this);
}

TEST_F(ChainManagerTests, gameEventAbandonsAfterMax) {
    chainGameEventAbandonsAfterMax(this);
}

TEST_F(ChainManagerTests, claimsCoordinatorWhenSelfIsLowestMacInLoop) {
    chainClaimsCoordinatorWhenSelfIsLowestMacInLoop(this);
}
TEST_F(ChainManagerTests, demotesCoordinatorWhenLowerMacJoins) {
    chainDemotesCoordinatorWhenLowerMacJoins(this);
}
TEST_F(ChainManagerTests, doesNotClaimWithoutLoop) {
    chainDoesNotClaimWithoutLoop(this);
}
TEST_F(ChainManagerTests, oneSecondMinStabilityGuard) {
    chainOneSecondMinStabilityGuard(this);
}
TEST_F(ChainManagerTests, confirmDroppedWhenTopologyUnstable) {
    chainConfirmDroppedWhenTopologyUnstable(this);
}

TEST_F(ChainMultiDeviceFixture, chainFormsAndElectsChampion) {
    chainMultiDeviceChainFormsAndElectsChampion(this);
}

TEST_F(ChainMultiDeviceFixture, confirmDeliveredToChampion) {
    chainMultiDeviceConfirmDeliveredToChampion(this);
}

TEST_F(ChainMultiDeviceFixture, autoConfirmReachesChampion) {
    chainMultiDeviceAutoConfirmReachesChampion(this);
}

// Ring/loop detection flows through RDC::isInLoop() + isTopologyStable(), so
// these tests advance the clock via primeTopologyStableAll() rather than driving
// connect-edge events.
TEST_F(ChainMultiDeviceFixture, shootoutFourDeviceFullTournament) {
    shootoutFourDeviceFullTournament(this);
}
TEST_F(ChainMultiDeviceFixture, shootoutEightDeviceFullTournament) {
    shootoutEightDeviceFullTournament(this);
}
TEST_F(ChainMultiDeviceFixture, shootoutSixteenDeviceFullTournament) {
    shootoutSixteenDeviceFullTournament(this);
}
TEST_F(ChainMultiDeviceFixture, shootoutFourDeviceConsensusAndMatchStart) {
    shootoutFourDeviceConsensusAndMatchStart(this);
}
TEST_F(ChainMultiDeviceFixture, shootoutFourDeviceTwoTournamentsBackToBack) {
    shootoutFourDeviceTwoTournamentsBackToBack(this);
}
TEST_F(ChainMultiDeviceFixture, ThreeDeviceChainNoLoop) { rdcThreeDeviceChainNoLoop(this); }
TEST_F(ChainMultiDeviceFixture, ThreeDeviceRingReportsLoop) { rdcThreeDeviceRingReportsLoop(this); }
TEST_F(ChainMultiDeviceFixture, HunterRingClaimsExactlyOneCoordinator) { chainHunterRingClaimsExactlyOneCoordinator(this); }
TEST_F(ChainMultiDeviceFixture, CoordinatorIsNeverSupporter) { chainCoordinatorIsNeverSupporter(this); }
TEST_F(ChainMultiDeviceFixture, MixedRoleRingClaimsCoordinator) { chainMixedRoleRingClaimsCoordinator(this); }
TEST_F(ChainMultiDeviceFixture, DeviceTypePropagatesViaHello) { chainDeviceTypePropagatesViaHello(this); }
TEST_F(ChainMultiDeviceFixture, UserIdPropagatesViaHello) { chainUserIdPropagatesViaHello(this); }
TEST_F(ChainMultiDeviceFixture, TwoDeviceBothJacksIsLoop) { chainTwoDeviceBothJacksIsLoop(this); }
TEST_F(ChainMultiDeviceFixture, FdnPeerReportsFdnDeviceType) { chainFdnPeerReportsFdnDeviceType(this); }
TEST_F(ChainMultiDeviceFixture, FdnMidChainSeversDuelChain) { chainFdnMidChainSeversDuelChain(this); }
TEST_F(ChainMultiDeviceFixture, SelfHelloEchoRejected) { chainSelfHelloEchoRejected(this); }
TEST_F(ChainMultiDeviceFixture, ChampionChainLengthFromTopology) { chainChampionChainLengthFromTopology(this); }
TEST_F(ChainMultiDeviceFixture, MixedChainRolesConverge) { chainMixedChainRolesConverge(this); }
TEST_F(ChainMultiDeviceFixture, RingYankLeavesOneChampion) { chainRingYankLeavesOneChampion(this); }
TEST_F(ChainMultiDeviceFixture, HalfOpenLinkNoPhantomEdge) { chainHalfOpenLinkNoPhantomEdge(this); }
TEST_F(ChainMultiDeviceFixture, HalfOpenRingResolvesChampion) { chainHalfOpenRingResolvesChampion(this); }
TEST_F(ChainMultiDeviceFixture, SelfHelloDoesNotRefreshSilentLink) { chainSelfHelloDoesNotRefreshSilentLink(this); }
TEST_F(ChainMultiDeviceFixture, sixDeviceMixedLoopTailToTailFirst) { chainSixDeviceMixedLoopTailToTailFirst(this); }
TEST_F(ChainMultiDeviceFixture, sixDeviceMixedLoopHeadToHeadFirst) { chainSixDeviceMixedLoopHeadToHeadFirst(this); }
TEST_F(ChainMultiDeviceFixture, fourDeviceMixedLoopTailToTailFirst) {
    chainFourDeviceMixedLoopTailToTailFirst(this);
}
TEST_F(ChainMultiDeviceFixture, fourDeviceMixedLoopHeadToHeadFirst) {
    chainFourDeviceMixedLoopHeadToHeadFirst(this);
}
TEST_F(ChainMultiDeviceFixture, mixedLoopCableYankClearsLoopMergeState) {
    chainMixedLoopCableYankClearsLoopMergeState(this);
}
TEST_F(ChainMultiDeviceFixture, sixteenDeviceRingFullBracket) {
    chainSixteenDeviceRingFullBracket(this);
}
TEST_F(ChainMultiDeviceFixture, cableYankSixteenRingConverges) {
    chainCableYankSixteenRingConverges(this);
}
TEST_F(ChainMultiDeviceFixture, cableYankFiftyRingConverges) {
    chainCableYankFiftyRingConverges(this);
}
TEST_F(ChainMultiDeviceFixture, idleCadenceSettlesToOneHz) {
    chainIdleCadenceSettlesToOneHz(this);
}


TEST_F(ShootoutManagerTests, bracketSizeAndByeMatchMemberCount) { bracketSizeAndByeMatchMemberCount(this); }
TEST_F(ShootoutManagerTests, localConfirmIsRecordedAndBroadcast) { localConfirmIsRecordedAndBroadcast(this); }
TEST_F(ShootoutManagerTests, receivingAllConfirmsAdvancesToBracketReveal) { receivingAllConfirmsAdvancesToBracketReveal(this); }
TEST_F(ShootoutManagerTests, confirmRetriesUntilAckedDuringProposal) { confirmRetriesUntilAckedDuringProposal(this); }
TEST_F(ShootoutManagerTests, nonCoordinatorDoesNotAdvanceOnConfirmTally) { nonCoordinatorDoesNotAdvanceOnConfirmTally(this); }
TEST_F(ShootoutManagerTests, bracketAckClearsPendingForThatPeer) { bracketAckClearsPendingForThatPeer(this); }
TEST_F(ShootoutManagerTests, bracketRetriesThreeTimesThenAborts) { bracketRetriesThreeTimesThenAborts(this); }
TEST_F(ShootoutManagerTests, matchStartAbandonAbortsTournament) { matchStartAbandonAbortsTournament(this); }
TEST_F(ShootoutManagerTests, matchResultAbandonAbortsTournament) { matchResultAbandonAbortsTournament(this); }
TEST_F(ShootoutManagerTests, oversizedBracketAbortsTournament) { oversizedBracketAbortsTournament(this); }
TEST_F(ShootoutManagerTests, matchResultAbandonAfterTournamentEndStaysEnded) { matchResultAbandonAfterTournamentEndStaysEnded(this); }
TEST_F(ShootoutManagerTests, abortReceivedAfterTournamentEndStaysEnded) { abortReceivedAfterTournamentEndStaysEnded(this); }
TEST_F(ShootoutManagerTests, matchStartGatedOnAllBracketAcks) { matchStartGatedOnAllBracketAcks(this); }
TEST_F(ShootoutManagerTests, nonCoordinatorReceivingMatchStartIdentifiesRole) { nonCoordinatorReceivingMatchStartIdentifiesRole(this); }
TEST_F(ShootoutManagerTests, lowerMacBracketEntryStandsDownAndAdoptsPostBracket) { lowerMacBracketEntryStandsDownAndAdoptsPostBracket(this); }
TEST_F(ShootoutManagerTests, higherMacBracketEntryDoesNotUnseatCoordinator) { higherMacBracketEntryDoesNotUnseatCoordinator(this); }
TEST_F(ShootoutManagerTests, winnerBroadcastsMatchResultAndAdvancesLocally) { winnerBroadcastsMatchResultAndAdvancesLocally(this); }
TEST_F(ShootoutManagerTests, nonCoordinatorWinnerBroadcastsMatchResult) { nonCoordinatorWinnerBroadcastsMatchResult(this); }
TEST_F(ShootoutManagerTests, matchResultReceivedAdvancesLocalBracket) { matchResultReceivedAdvancesLocalBracket(this); }
TEST_F(ShootoutManagerTests, peerLostCoordinatorAborts) { peerLostCoordinatorAborts(this); }
TEST_F(ShootoutManagerTests, peerLostActiveDuelistAborts) { peerLostActiveDuelistAborts(this); }
TEST_F(ShootoutManagerTests, peerLostSpectatorAborts) { peerLostSpectatorAborts(this); }
TEST_F(ShootoutManagerTests, finalMatchResultTriggersTournamentEnd) { finalMatchResultTriggersTournamentEnd(this); }
TEST_F(ShootoutManagerTests, startProposalClearsAllPriorTournamentState) { startProposalClearsAllPriorTournamentState(this); }
TEST_F(ShootoutManagerTests, tournamentEndRetriesUntilAcked) { tournamentEndRetriesUntilAcked(this); }
TEST_F(ShootoutManagerTests, abortRetriesUntilAcked) { abortRetriesUntilAcked(this); }
TEST_F(ShootoutManagerTests, matchResultRetriesUntilAcked) { matchResultRetriesUntilAcked(this); }
TEST_F(ShootoutManagerTests, duplicateMatchResultDoesNotDoubleAdvance) { duplicateMatchResultDoesNotDoubleAdvance(this); }
TEST_F(ShootoutManagerTests, confirmRecordsPeerName) { confirmRecordsPeerName(this); }
TEST_F(ShootoutManagerTests, shootoutDoesNotMutateGlobalRole) { shootoutDoesNotMutateGlobalRole(this); }
TEST_F(ShootoutManagerTests, localRDCDisconnectIsIdempotent) { localRDCDisconnectIsIdempotent(this); }
TEST_F(ShootoutManagerTests, shootoutProposalDebouncesTransientLoopBreak) { shootoutProposalDebouncesTransientLoopBreak(this); }
TEST_F(ShootoutManagerTests, shootoutBracketRevealDebouncesTransientLoopBreak) { shootoutBracketRevealDebouncesTransientLoopBreak(this); }
TEST_F(ShootoutManagerTests, buildLoopMemberSetEmptyWhenTopologyUnstable) { shootoutBuildLoopMemberSetEmptyWhenTopologyUnstable(this); }
TEST_F(ShootoutManagerTests, buildLoopMemberSetReturnsChainMembersWhenStable) { shootoutBuildLoopMemberSetReturnsChainMembersWhenStable(this); }

// ============================================
// MAIN
// ============================================

int main(int argc, char **argv)
{
    ::testing::InitGoogleMock(&argc, argv);

    IdGenerator::initialize(42);

    if (RUN_ALL_TESTS())
        ;

    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif
