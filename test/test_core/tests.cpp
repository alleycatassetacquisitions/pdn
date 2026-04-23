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
#include "hwm-tests.hpp"
#include "rdc-tests.hpp"
#include "chain-duel-manager-tests.hpp"
#include "chain-duel-multi-device-fixture.hpp"
#include "peer-comms-types-tests.hpp"
#include "shootout-manager-tests.hpp"
#include "match-manager-concurrent.hpp"

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
    // Run tests
    if (RUN_ALL_TESTS())
        ;

    // sleep for 1 sec
    delay(1000);
}

#else

// ============================================
// PEER COMMS TYPES TESTS
// ============================================

TEST_F(PeerCommsTypesTestSuite, roleAnnouncePayloadHasCorrectSize) {
    roleAnnouncePayloadHasCorrectSize();
}

TEST_F(PeerCommsTypesTestSuite, roleAnnounceAckPayloadHasCorrectSize) {
    roleAnnounceAckPayloadHasCorrectSize();
}

TEST_F(PeerCommsTypesTestSuite, roleAnnouncePayloadIsPacked) {
    roleAnnouncePayloadIsPacked();
}

TEST_F(PeerCommsTypesTestSuite, roleAnnounceAckPayloadIsPacked) {
    roleAnnounceAckPayloadIsPacked();
}

TEST_F(PeerCommsTypesTestSuite, packetTypeEnumHasCorrectValues) {
    packetTypeEnumHasCorrectValues();
}

TEST_F(PeerCommsTypesTestSuite, numPacketTypesIsSequentialAfterAck) {
    numPacketTypesIsSequentialAfterAck();
}

TEST_F(PeerCommsTypesTestSuite, roleAnnouncePayloadFieldsAligned) {
    roleAnnouncePayloadFieldsAligned();
}

TEST_F(PeerCommsTypesTestSuite, roleAnnounceAckPayloadFieldsAligned) {
    roleAnnounceAckPayloadFieldsAligned();
}

TEST_F(PeerCommsTypesTestSuite, shootoutAckPayloadHasCorrectSize) {
    shootoutAckPayloadHasCorrectSize();
}

TEST_F(PeerCommsTypesTestSuite, shootoutCmdEnumHasExpectedValues) {
    shootoutCmdEnumHasExpectedValues();
}

TEST_F(PeerCommsTypesTestSuite, packetTypeEnumIncludesShootoutSlots) {
    packetTypeEnumIncludesShootoutSlots();
}

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
    // Should not crash with empty config
    device->loop();
    device->loop();
    
    // No assertions needed - just checking it doesn't crash
    SUCCEED();
}

TEST_F(DeviceTestSuite, setActiveAppSwitchesToNewApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    // Switch to app two
    device->setActiveApp(APP_TWO);
    
    ASSERT_EQ(appOne->pausedCount, 1);
    ASSERT_EQ(appTwo->mountedCount, 1);
}

TEST_F(DeviceTestSuite, setActiveAppPausesCurrentApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    ASSERT_EQ(appOne->pausedCount, 0);
    
    device->setActiveApp(APP_TWO);
    
    ASSERT_EQ(appOne->pausedCount, 1);
    ASSERT_TRUE(appOne->wasPaused);
}

TEST_F(DeviceTestSuite, setActiveAppMountsNewAppIfNotPreviouslyLaunched) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    device->setActiveApp(APP_TWO);
    
    ASSERT_EQ(appTwo->mountedCount, 1);
    ASSERT_EQ(appTwo->resumedCount, 0);
}

TEST_F(DeviceTestSuite, setActiveAppResumesIfPreviouslyPaused) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    // Switch to app two
    device->setActiveApp(APP_TWO);
    
    // Switch back to app one
    device->setActiveApp(APP_ONE);
    
    // App one should be resumed, not mounted again
    ASSERT_EQ(appOne->mountedCount, 1); // Only initial mount
    ASSERT_EQ(appOne->resumedCount, 1);  // Resume on switch back
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
    ASSERT_EQ(appOne->pausedCount, 1);
    ASSERT_EQ(appTwo->mountedCount, 1);
    
    // Run some loops on app two
    device->loop();
    ASSERT_EQ(appTwo->loopCount, 1);
    
    // Switch back to app one
    device->setActiveApp(APP_ONE);
    ASSERT_EQ(appTwo->pausedCount, 1);
    ASSERT_EQ(appOne->resumedCount, 1);
    
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
    
    // APP_THREE -> APP_ONE (resume)
    device->setActiveApp(APP_ONE);
    ASSERT_EQ(appOne->resumedCount, 1);
    
    // APP_ONE -> APP_TWO (resume)
    device->setActiveApp(APP_TWO);
    ASSERT_EQ(appTwo->resumedCount, 1);
}

TEST_F(DeviceTestSuite, pausedAppPreservesState) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    
    device->loadAppConfig(std::move(config), APP_ONE);
    
    // Run some loops to build state
    device->loop();
    device->loop();
    device->loop();
    int loopsBeforePause = appOne->loopCount;
    
    // Switch away
    device->setActiveApp(APP_TWO);
    
    // Run loops on app two
    device->loop();
    device->loop();
    
    // App one's loop count should not have changed
    ASSERT_EQ(appOne->loopCount, loopsBeforePause);
    
    // Switch back
    device->setActiveApp(APP_ONE);
    
    // Continue loops
    device->loop();
    
    // Loop count should continue from where it left off
    ASSERT_EQ(appOne->loopCount, loopsBeforePause + 1);
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

TEST_F(SerialTestSuite, serialWriteAppendsStringStart) {
    serialWriteAppendsStringStart();
}

TEST_F(SerialTestSuite, headIsSetWhenPeekIsExecutedAndStringIsRemovedFromQueue) {
    headIsSetWhenPeekIsExecutedAndStringIsRemovedFromQueue();
}

TEST_F(SerialTestSuite, whenHeadIsEmptyReadStringStillReturnsNextString) {
    whenHeadIsEmptyReadStringStillReturnsNextString();
}

// ============================================
// PLAYER TESTS
// ============================================

TEST_F(PlayerTestSuite, jsonRoundTripPreservesAllFields) {
    playerJsonRoundTripPreservesAllFields(player);
}

TEST_F(PlayerTestSuite, jsonRoundTripWithBountyRole) {
    playerJsonRoundTripWithBountyRole(player);
}

TEST_F(PlayerTestSuite, statsIncrementCorrectly) {
    playerStatsIncrementCorrectly(player);
}

TEST_F(PlayerTestSuite, streakResetsOnLoss) {
    playerStreakResetsOnLoss(player);
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

TEST_F(MatchTestSuite, binaryRoundTripPreservesAllFields) {
    matchBinaryRoundTripPreservesAllFields();
}

TEST_F(MatchTestSuite, binarySizeIsCorrect) {
    matchBinarySizeIsCorrect();
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

TEST_F(MatchTestSuite, withLargeDrawTimes) {
    matchWithLargeDrawTimes();
}

// // ============================================
// // UUID TESTS
// // ============================================

// TEST_F(UUIDTestSuite, stringToBytesProducesCorrectOutput) {
//     uuidStringToBytesProducesCorrectOutput();
// }

// TEST_F(UUIDTestSuite, bytesToStringProducesValidFormat) {
//     uuidBytesToStringProducesValidFormat();
// }

// TEST_F(UUIDTestSuite, roundTripPreservesData) {
//     uuidRoundTripPreservesData();
// }

// TEST_F(UUIDTestSuite, generatorProducesValidFormat) {
//     uuidGeneratorProducesValidFormat();
// }

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

TEST_F(MatchManagerTestSuite, setBoostStoresValue) {
    matchManagerSetBoostStoresValue(matchManager, player);
}

TEST_F(MatchManagerTestSuite, boostSubtractedFromHunterReactionTime) {
    matchManagerBoostSubtractedFromHunterReactionTime(this);
}

TEST_F(MatchManagerTestSuite, clearCurrentMatchResetsBoost) {
    matchManagerClearCurrentMatchResetsBoost(matchManager, player);
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

TEST_F(MatchManagerTestSuite, tracksDuelState) {
    matchManagerTracksDuelState(matchManager, player);
}

TEST_F(MatchManagerTestSuite, graceExpiredAloneFinalizes) {
    matchManagerGraceExpiredAloneFinalizes(matchManager, player);
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
    matchManagerClearCurrentMatchResetsMasherCount(matchManager, player);
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

// ============================================
// MATCH MANAGER CONCURRENCY TESTS (TSan)
// ============================================

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

TEST_F(DuelIntegrationTestSuite, matchSerializationRoundTrip) {
    matchSerializationRoundTrip();
}

TEST_F(DuelIntegrationTestSuite, playerStatsAccumulateAcrossMatches) {
    playerStatsAccumulateAcrossMatches(hunter);
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

TEST_F(IdleStateTests, buttonCallbacksRegisteredAndRemoved) {
    idleButtonCallbacksRegisteredAndRemoved(this);
}

TEST_F(IdleStateTests, doesNotTransitionWithMatchButNotReady) {
    idleDoesNotTransitionWithMatchButNotReady(this);
}

TEST_F(IdleStateTests, transitionsToDuelCountdownWhenMatchIsReady) {
    idleTransitionsToDuelCountdownWhenMatchIsReady(this);
}

// ============================================
// QUICKDRAW STATE TESTS - HANDSHAKE
// ============================================

TEST_F(HandshakeStateTests, outputIdleTransitionsOnMacReceived) {
    outputIdleTransitionsOnMacReceived(this);
}

TEST_F(HandshakeStateTests, outputIdleIgnoresUnrelatedSerial) {
    outputIdleIgnoresUnrelatedSerial(this);
}

TEST_F(HandshakeStateTests, outputIdleClearsCallbackOnDismount) {
    outputIdleClearsCallbackOnDismount(this);
}

TEST_F(HandshakeStateTests, outputSendIdTransitionsOnExchangeIdAck) {
    outputSendIdTransitionsOnExchangeIdAck(this);
}

TEST_F(HandshakeStateTests, outputSendIdClearsOnDismount) {
    outputSendIdClearsOnDismount(this);
}

TEST_F(HandshakeStateTests, inputIdleTransitionsOnExchangeId) {
    inputIdleTransitionsOnExchangeId(this);
}

TEST_F(HandshakeStateTests, inputSendIdTransitionsOnExchangeIdAck) {
    inputSendIdTransitionsOnExchangeIdAck(this);
}

TEST_F(HandshakeStateTests, inputSendIdClearsOnDismount) {
    inputSendIdClearsOnDismount(this);
}

TEST_F(HandshakeStateTests, handshakeAppOutputJackTimeoutResetsToIdle) {
    handshakeAppOutputJackTimeoutResetsToIdle(this);
}

// ============================================
// QUICKDRAW STATE TESTS - COUNTDOWN
// ============================================

TEST_F(DuelCountdownTests, buttonMasherPenaltyIncrementsOnButtonPress) {
    countdownButtonMasherPenaltyIncrementsOnButtonPress(this);
}

TEST_F(DuelCountdownTests, multipleEarlyPressesAccumulatePenalty) {
    countdownMultipleEarlyPressesAccumulatePenalty(this);
}

TEST_F(DuelCountdownTests, progressesThroughStages) {
    countdownProgressesThroughStages(this);
}

TEST_F(DuelCountdownTests, battleTransitionSetsFlag) {
    countdownBattleTransitionSetsFlag(this);
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

TEST_F(StateCleanupTests, handshakeClearsWirelessCallbacks) {
    cleanupHandshakeClearsWirelessCallbacks(this);
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

TEST_F(StateCleanupTests, duelPushedDebouncesTransientDisconnect) {
    duelPushedDebouncesTransientDisconnect(this);
}

TEST_F(StateCleanupTests, duelReceivedResultDebouncesTransientDisconnect) {
    duelReceivedResultDebouncesTransientDisconnect(this);
}

// ============================================
// QUICKDRAW STATE TESTS - CONNECTION SUCCESSFUL
// ============================================

TEST_F(ConnectionSuccessfulTests, transitionsAfterThreshold) {
    connectionSuccessfulTransitionsAfterThreshold(this);
}

TEST_F(QuickdrawLifecycleTests, ctorDtorDoesNotLeak) {
    quickdrawCtorDtorDoesNotLeak(this);
}

// ============================================
// QUICKDRAW INTEGRATION TESTS - PACKET PARSING
// ============================================

TEST_F(PacketParsingTests, drawResultInvokesCallback) {
    packetParsingDrawResultInvokesCallback(this);
}

TEST_F(PacketParsingTests, neverPressedParsesCorrectly) {
    packetParsingNeverPressedParsesCorrectly(this);
}

TEST_F(PacketParsingTests, rejectsMalformedPacket) {
    packetParsingRejectsMalformedPacket(this);
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

// ============================================
// QUICKDRAW INTEGRATION TESTS - HANDSHAKE
// ============================================

TEST_F(HandshakeIntegrationTests, completeBountyPerspective) {
    handshakeCompleteBountyPerspective(this);
}

TEST_F(HandshakeIntegrationTests, completeHunterPerspective) {
    handshakeCompleteHunterPerspective(this);
}

TEST_F(HandshakeIntegrationTests, twoDeviceFullFlow) {
    handshakeTwoDeviceFullFlow(this);
}

TEST_F(HandshakeIntegrationTests, timeoutBeforeCompletion) {
    handshakeTimeoutBeforeCompletion(this);
}

TEST_F(HandshakeIntegrationTests, rejectsInvalidPacketData) {
    handshakeRejectsInvalidPacketData(this);
}

TEST_F(HandshakeIntegrationTests, ignoresUnexpectedCommands) {
    handshakeIgnoresUnexpectedCommands(this);
}

TEST_F(HandshakeIntegrationTests, setsOpponentMacAddress) {
    handshakeSetsOpponentMacAddress(this);
}

TEST_F(HandshakeIntegrationTests, matchDataPropagatedCorrectly) {
    handshakeMatchDataPropagatedCorrectly(this);
}

// ============================================
// HWM UNIT TESTS
// ============================================

TEST_F(HWMUnitTests, getMacPeerReturnsNullWhenNotSet) {
    hwmGetMacPeerReturnsNullWhenNotSet(this);
}

TEST_F(HWMUnitTests, getMacPeerReturnsCorrectMac) {
    hwmGetMacPeerReturnsCorrectMac(this);
}

TEST_F(HWMUnitTests, removeMacPeerClearsEntry) {
    hwmRemoveMacPeerClearsEntry(this);
}

TEST_F(HWMUnitTests, sendPacketFailsWithNoPeer) {
    hwmSendPacketFailsWithNoPeer(this);
}

TEST_F(HWMUnitTests, clearCallbacksRemovesAll) {
    hwmClearCallbacksRemovesAll(this);
}

TEST_F(HWMUnitTests, processRejectsNegativeCommand) {
    hwmProcessRejectsNegativeCommand(this);
}

// ============================================
// REMOTE DEVICE COORDINATOR TESTS
// ============================================

TEST_F(RDCTests, defaultStateIsDisconnectedOnAllPorts) {
    rdcDefaultStateIsDisconnectedOnAllPorts(this);
}

TEST_F(RDCTests, outputPortConnectionLifecycle) {
    rdcOutputPortConnectionLifecycle(this);
}

TEST_F(RDCTests, connectedPortDisconnectsOnHeartbeatTimeout) {
    rdcConnectedPortDisconnectsOnHeartbeatTimeout(this);
}

TEST_F(RDCTests, chainAnnouncementFiltersSelfMac) {
    rdcChainAnnouncementFiltersSelfMac(this);
}

TEST_F(RDCTests, daisyChainCappedAtMaxPeers) {
    rdcDaisyChainCappedAtMaxPeers(this);
}

TEST_F(RDCTests, disconnectWipesDaisyChainedPeers) {
    rdcDisconnectWipesDaisyChainedPeers(this);
}

TEST_F(RDCTests, ignoresAnnouncementFromNonDirectPeer) {
    rdcIgnoresAnnouncementFromNonDirectPeer(this);
}

TEST_F(RDCTests, chainChangeCallbackFiresOnDaisyAdded) {
    rdcChainChangeCallbackFiresOnDaisyAdded(this);
}

TEST_F(RDCTests, doesNotEmitWhenOtherPortDisconnected) {
    rdcDoesNotEmitWhenOtherPortDisconnected(this);
}

TEST_F(RDCTests, midChainEmitsForwardAndBackward) {
    rdcMidChainEmitsForwardAndBackward(this);
}

TEST_F(RDCTests, duplicateAnnouncementDoesNotFireCallback) {
    rdcDuplicateAnnouncementDoesNotFireCallback(this);
}

TEST_F(RDCTests, directPeerRegistrationEmitsBackwardAnnouncement) {
    rdcDirectPeerRegistrationEmitsBackwardAnnouncement(this);
}

TEST_F(RDCTests, directPeerDropEmitsAnnouncement) {
    rdcDirectPeerDropEmitsAnnouncement(this);
}

TEST_F(RDCTests, directPeerDropFiresPeerLostCallbackWithMac) {
    rdcDirectPeerDropFiresPeerLostCallbackWithMac(this);
}

TEST_F(RDCTests, chainAnnouncementPacketHandlerUpdatesDaisyChain) {
    rdcChainAnnouncementPacketHandlerUpdatesDaisyChain(this);
}

TEST_F(RDCTests, ackedAnnouncementDoesNotRetransmit) {
    rdcAckedAnnouncementDoesNotRetransmit(this);
}

TEST_F(RDCTests, announcementAbandonedAfterMaxRetries) {
    rdcAnnouncementAbandonedAfterMaxRetries(this);
}

// ============================================
// CHAIN DUEL MANAGER TESTS
// ============================================

TEST_F(ChainDuelManagerTests, roleDerivationWithChampionTopology) {
    cdmRoleDerivationWithChampionTopology(this);
}

TEST_F(ChainDuelManagerTests, canInitiateMatchFalseForBounty) {
    cdmCanInitiateMatchFalseForBounty(this);
}

TEST_F(ChainDuelManagerTests, confirmLifecycle) {
    cdmConfirmLifecycle(this);
}

TEST_F(ChainDuelManagerTests, onChainStateChangedClearsOnDrain) {
    cdmOnChainStateChangedClearsOnDrain(this);
}

TEST_F(ChainDuelManagerTests, confirmFromUnknownOriginatorRejected) {
    cdmConfirmFromUnknownOriginatorRejected(this);
}

TEST_F(ChainDuelManagerTests, isChampionFalseWithSameRoleOpponent) {
    cdmIsChampionFalseWithSameRoleOpponent(this);
}

TEST_F(ChainDuelManagerTests, isChampionFalseInRing) {
    cdmIsChampionFalseInRing(this);
}

TEST_F(ChainDuelManagerTests, sendConfirmTargetsChampionMac) {
    cdmSendConfirmTargetsChampionMac(this);
}

TEST_F(ChainDuelManagerTests, sendConfirmIncrementsSeqId) {
    cdmSendConfirmIncrementsSeqId(this);
}

TEST_F(ChainDuelManagerTests, sendConfirmNoopWhenChampionMacInvalid) {
    cdmSendConfirmNoopWhenChampionMacInvalid(this);
}

TEST_F(ChainDuelManagerTests, roleAnnounceUpdatesChampionMac) {
    cdmRoleAnnounceUpdatesChampionMac(this);
}

TEST_F(ChainDuelManagerTests, roleAnnounceNoCascadeIfChampionUnchanged) {
    cdmRoleAnnounceNoCascadeIfChampionUnchanged(this);
}

TEST_F(ChainDuelManagerTests, broadcastRoleAndChampionSends) {
    cdmBroadcastRoleAndChampionSends(this);
}

TEST_F(ChainDuelManagerTests, ackClearsPending) {
    cdmAckClearsPending(this);
}

TEST_F(ChainDuelManagerTests, ackFromWrongMacIgnored) {
    cdmAckFromWrongMacIgnored(this);
}

TEST_F(ChainDuelManagerTests, retryStatsRecordsLifecycle) {
    cdmRetryStatsRecordsLifecycle(this);
}

TEST_F(ChainDuelManagerTests, retransmitAbandonsAfterMax) {
    cdmRetransmitAbandonsAfterMax(this);
}

TEST_F(ChainDuelManagerTests, onChainStateBecomesChampionSetsSelfMac) {
    cdmOnChainStateBecomesChampionSetsSelfMac(this);
}

TEST_F(ChainDuelManagerTests, supporterKeepsUpstreamChampionMacAfterTransition) {
    cdmSupporterKeepsUpstreamChampionMacAfterTransition(this);
}

TEST_F(ChainDuelManagerTests, onChainStateNewSupporterTriggersBroadcast) {
    cdmOnChainStateNewSupporterTriggersBroadcast(this);
}

TEST_F(ChainDuelManagerTests, chainDuelThreeDeviceConfirm) {
    chainDuelThreeDeviceConfirm(this);
}

TEST_F(ChainDuelManagerTests, chainDuelReconfigRecovers) {
    chainDuelReconfigRecovers(this);
}

TEST_F(ChainDuelManagerTests, roleAnnounceFromSupporterJackIgnoresChampionMac) {
    cdmRoleAnnounceFromSupporterJackIgnoresChampionMac(this);
}

TEST_F(ChainDuelManagerTests, broadcastToOpponentJackPopulatesRemoteRole) {
    cdmBroadcastToOpponentJackPopulatesRemoteRole(this);
}

TEST_F(ChainDuelManagerTests, championToSupporterClearsStaleSelfMac) {
    cdmChampionToSupporterClearsStaleSelfMac(this);
}

TEST_F(ChainDuelManagerTests, roleAnnounceFromOppositeRoleOpponentIgnoresChampionMac) {
    cdmRoleAnnounceFromOppositeRoleOpponentIgnoresChampionMac(this);
}

TEST_F(ChainDuelManagerTests, gameEventCountdownIsFireAndForget) {
    cdmGameEventCountdownIsFireAndForget(this);
}

TEST_F(ChainDuelManagerTests, gameEventWinIsTrackedAndRetried) {
    cdmGameEventWinIsTrackedAndRetried(this);
}

TEST_F(ChainDuelManagerTests, gameEventAckClearsPending) {
    cdmGameEventAckClearsPending(this);
}

TEST_F(ChainDuelManagerTests, gameEventAbandonsAfterMax) {
    cdmGameEventAbandonsAfterMax(this);
}

// ============================================
// CHAIN DUEL MULTI-DEVICE FIXTURE TESTS
// ============================================

TEST_F(ChainDuelMultiDeviceFixture, chainFormsAndElectsChampion) {
    cdmMultiDeviceChainFormsAndElectsChampion(this);
}

TEST_F(ChainDuelMultiDeviceFixture, confirmDeliveredToChampion) {
    cdmMultiDeviceConfirmDeliveredToChampion(this);
}

TEST_F(ChainDuelMultiDeviceFixture, shootoutFourDeviceFullTournament) {
    shootoutFourDeviceFullTournament(this);
}
TEST_F(ChainDuelMultiDeviceFixture, shootoutEightDeviceFullTournament) {
    shootoutEightDeviceFullTournament(this);
}
TEST_F(ChainDuelMultiDeviceFixture, shootoutFourDeviceConsensusAndMatchStart) {
    shootoutFourDeviceConsensusAndMatchStart(this);
}

// ============================================
// SHOOTOUT MANAGER TESTS
// ============================================

TEST_F(ShootoutManagerTests, coordinatorIsLowestMacAmongConfirmed) { coordinatorIsLowestMacAmongConfirmed(this); }
TEST_F(ShootoutManagerTests, bracketSizeAndByeMatchMemberCount) { bracketSizeAndByeMatchMemberCount(this); }
TEST_F(ShootoutManagerTests, localConfirmIsRecordedAndBroadcast) { localConfirmIsRecordedAndBroadcast(this); }
TEST_F(ShootoutManagerTests, receivingAllConfirmsAdvancesToBracketReveal) { receivingAllConfirmsAdvancesToBracketReveal(this); }
TEST_F(ShootoutManagerTests, confirmRebroadcastsEverySecondDuringProposal) { confirmRebroadcastsEverySecondDuringProposal(this); }
TEST_F(ShootoutManagerTests, coordinatorBroadcastsBracketOnAdvance) { coordinatorBroadcastsBracketOnAdvance(this); }
TEST_F(ShootoutManagerTests, bracketAckClearsPendingForThatPeer) { bracketAckClearsPendingForThatPeer(this); }
TEST_F(ShootoutManagerTests, bracketRetriesThreeTimesThenAborts) { bracketRetriesThreeTimesThenAborts(this); }
TEST_F(ShootoutManagerTests, matchStartGatedOnAllBracketAcks) { matchStartGatedOnAllBracketAcks(this); }
TEST_F(ShootoutManagerTests, nonCoordinatorReceivingMatchStartIdentifiesRole) { nonCoordinatorReceivingMatchStartIdentifiesRole(this); }
TEST_F(ShootoutManagerTests, winnerBroadcastsMatchResultAndAdvancesLocally) { winnerBroadcastsMatchResultAndAdvancesLocally(this); }
TEST_F(ShootoutManagerTests, matchResultReceivedAdvancesLocalBracket) { matchResultReceivedAdvancesLocalBracket(this); }
TEST_F(ShootoutManagerTests, drawWatchdogReplaysMatchStart) { drawWatchdogReplaysMatchStart(this); }
TEST_F(ShootoutManagerTests, peerLostCoordinatorAborts) { peerLostCoordinatorAborts(this); }
TEST_F(ShootoutManagerTests, peerLostActiveDuelistOpponentWins) { peerLostActiveDuelistOpponentWins(this); }
TEST_F(ShootoutManagerTests, peerLostSpectatorMarksForfeit) { peerLostSpectatorMarksForfeit(this); }
TEST_F(ShootoutManagerTests, finalMatchResultTriggersTournamentEnd) { finalMatchResultTriggersTournamentEnd(this); }
TEST_F(ShootoutManagerTests, startProposalClearsAllPriorTournamentState) { startProposalClearsAllPriorTournamentState(this); }
TEST_F(ShootoutManagerTests, tournamentEndRetriesUntilAcked) { tournamentEndRetriesUntilAcked(this); }
TEST_F(ShootoutManagerTests, matchResultRetriesUntilAcked) { matchResultRetriesUntilAcked(this); }
TEST_F(ShootoutManagerTests, duplicateMatchResultDoesNotDoubleAdvance) { duplicateMatchResultDoesNotDoubleAdvance(this); }
TEST_F(ShootoutManagerTests, confirmRecordsPeerName) { confirmRecordsPeerName(this); }
TEST_F(ShootoutManagerTests, isHunterRestoredAfterTournament) { isHunterRestoredAfterTournament(this); }
TEST_F(ShootoutManagerTests, localRDCDisconnectIsIdempotent) { localRDCDisconnectIsIdempotent(this); }
TEST_F(ShootoutManagerTests, shootoutProposalDebouncesTransientLoopBreak) { shootoutProposalDebouncesTransientLoopBreak(this); }
TEST_F(ShootoutManagerTests, shootoutBracketRevealDebouncesTransientLoopBreak) { shootoutBracketRevealDebouncesTransientLoopBreak(this); }

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
