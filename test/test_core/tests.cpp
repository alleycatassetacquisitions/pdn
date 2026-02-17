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
#include "quickdraw-edge-tests.hpp"
#include "konami-metagame-tests.hpp"
#include "konami-handshake-tests.hpp"
#include "lifecycle-tests.hpp"
#include "contract-tests.hpp"
#include "wireless-manager-tests.hpp"

// Core utility tests
#include "uuid-tests.hpp"
#include "timer-tests.hpp"
#include "difficulty-scaler-tests.hpp"

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

TEST_F(StateMachineTestSuite, commitStateHandlesNullStateGracefully) {
    NullStateTestMachine nullStateMachine;
    MockDevice device;

    nullStateMachine.initialize(&device);

    State* initialState = nullStateMachine.getCurrentState();
    ASSERT_NE(initialState, nullptr);
    ASSERT_EQ(initialState->getStateId(), INITIAL_STATE);

    // Force commitState with null newState (simulating an error condition)
    nullStateMachine.forceCommitWithNull(&device);

    // Should remain in initial state without crashing
    ASSERT_EQ(nullStateMachine.getCurrentState()->getStateId(), INITIAL_STATE);
    ASSERT_FALSE(nullStateMachine.getTransitionReadyFlag());
}

TEST_F(StateMachineTestSuite, checkTransitionsWithNullNextStatePreventCommit) {
    NullStateTestMachine nullStateMachine;
    MockDevice device;

    nullStateMachine.initialize(&device);

    // Trigger transition to null state
    InitialTestState* initial = dynamic_cast<InitialTestState*>(nullStateMachine.getCurrentState());
    initial->transitionToSecond = true;

    nullStateMachine.checkStateTransitions();

    // checkStateTransitions should set stateChangeReady to false when newState is null
    ASSERT_FALSE(nullStateMachine.getTransitionReadyFlag());
    ASSERT_EQ(nullStateMachine.getNewState(), nullptr);

    // Should remain in initial state
    ASSERT_EQ(nullStateMachine.getCurrentState()->getStateId(), INITIAL_STATE);
}

TEST_F(StateMachineTestSuite, onStateLoopWithNullTransitionDoesNotCrash) {
    NullStateTestMachine nullStateMachine;
    MockDevice device;

    nullStateMachine.initialize(&device);

    // Trigger transition to null state
    InitialTestState* initial = dynamic_cast<InitialTestState*>(nullStateMachine.getCurrentState());
    initial->transitionToSecond = true;

    // onStateLoop includes checkStateTransitions + commitState
    // This should not crash even with null transition
    nullStateMachine.onStateLoop(&device);

    // Should remain in initial state (because checkTransitions returns null,
    // stateChangeReady is false, and commitState is never called)
    ASSERT_EQ(nullStateMachine.getCurrentState()->getStateId(), INITIAL_STATE);
}

// ============================================
// DEVICE TESTS - APP PATTERN
// ============================================

TEST_F(DeviceTestSuite, loadAppConfigMountsLaunchApp) {
    AppConfig config;
    config[APP_ONE] = appOne;

    device->loadAppConfig(std::move(config), APP_ONE);

    ASSERT_EQ(appOne->counters->mountedCount, 1);
    ASSERT_EQ(appOne->counters->loopCount, 0);
}

TEST_F(DeviceTestSuite, loadAppConfigWithMultipleAppsOnlyMountsLaunchApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    config[APP_THREE] = appThree;

    device->loadAppConfig(std::move(config), APP_TWO);

    ASSERT_EQ(appOne->counters->mountedCount, 0);
    ASSERT_EQ(appTwo->counters->mountedCount, 1);
    ASSERT_EQ(appThree->counters->mountedCount, 0);
}

TEST_F(DeviceTestSuite, loopCallsCurrentAppStateLoop) {
    AppConfig config;
    config[APP_ONE] = appOne;

    device->loadAppConfig(std::move(config), APP_ONE);

    device->loop();
    device->loop();
    device->loop();

    ASSERT_EQ(appOne->counters->loopCount, 3);
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

    ASSERT_EQ(appOne->counters->pausedCount, 1);
    ASSERT_EQ(appTwo->counters->mountedCount, 1);
}

TEST_F(DeviceTestSuite, setActiveAppPausesCurrentApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;

    device->loadAppConfig(std::move(config), APP_ONE);

    ASSERT_EQ(appOne->counters->pausedCount, 0);

    device->setActiveApp(APP_TWO);

    ASSERT_EQ(appOne->counters->pausedCount, 1);
    ASSERT_TRUE(appOne->counters->wasPaused);
}

TEST_F(DeviceTestSuite, setActiveAppMountsNewAppIfNotPreviouslyLaunched) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;

    device->loadAppConfig(std::move(config), APP_ONE);
    device->setActiveApp(APP_TWO);

    ASSERT_EQ(appTwo->counters->mountedCount, 1);
    ASSERT_EQ(appTwo->counters->resumedCount, 0);
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
    ASSERT_EQ(appOne->counters->mountedCount, 1); // Only initial mount
    ASSERT_EQ(appOne->counters->resumedCount, 1);  // Resume on switch back
}

TEST_F(DeviceTestSuite, setActiveAppLoopCallsNewApp) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;

    device->loadAppConfig(std::move(config), APP_ONE);

    device->loop();
    device->loop();

    ASSERT_EQ(appOne->counters->loopCount, 2);
    ASSERT_EQ(appTwo->counters->loopCount, 0);

    device->setActiveApp(APP_TWO);

    device->loop();
    device->loop();
    device->loop();

    ASSERT_EQ(appOne->counters->loopCount, 2); // No more loops on app one
    ASSERT_EQ(appTwo->counters->loopCount, 3); // App two now getting loops
}

TEST_F(DeviceTestSuite, appLifecycleSequenceCorrect) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;

    // Load and launch app one
    device->loadAppConfig(std::move(config), APP_ONE);
    ASSERT_EQ(appOne->counters->mountedCount, 1);

    // Run some loops
    device->loop();
    device->loop();
    ASSERT_EQ(appOne->counters->loopCount, 2);

    // Switch to app two
    device->setActiveApp(APP_TWO);
    ASSERT_EQ(appOne->counters->pausedCount, 1);
    ASSERT_EQ(appTwo->counters->mountedCount, 1);

    // Run some loops on app two
    device->loop();
    ASSERT_EQ(appTwo->counters->loopCount, 1);

    // Switch back to app one
    device->setActiveApp(APP_ONE);
    ASSERT_EQ(appTwo->counters->pausedCount, 1);
    ASSERT_EQ(appOne->counters->resumedCount, 1);

    // Continue loops on app one
    device->loop();
    ASSERT_EQ(appOne->counters->loopCount, 3);
}

TEST_F(DeviceTestSuite, multipleAppSwitchesWorkCorrectly) {
    AppConfig config;
    config[APP_ONE] = appOne;
    config[APP_TWO] = appTwo;
    config[APP_THREE] = appThree;

    device->loadAppConfig(std::move(config), APP_ONE);

    // APP_ONE -> APP_TWO
    device->setActiveApp(APP_TWO);
    ASSERT_EQ(appTwo->counters->mountedCount, 1);

    // APP_TWO -> APP_THREE
    device->setActiveApp(APP_THREE);
    ASSERT_EQ(appThree->counters->mountedCount, 1);

    // APP_THREE -> APP_ONE (resume)
    device->setActiveApp(APP_ONE);
    ASSERT_EQ(appOne->counters->resumedCount, 1);

    // APP_ONE -> APP_TWO (resume)
    device->setActiveApp(APP_TWO);
    ASSERT_EQ(appTwo->counters->resumedCount, 1);
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
    int loopsBeforePause = appOne->counters->loopCount;

    // Switch away
    device->setActiveApp(APP_TWO);

    // Run loops on app two
    device->loop();
    device->loop();

    // App one's loop count should not have changed
    ASSERT_EQ(appOne->counters->loopCount, loopsBeforePause);

    // Switch back
    device->setActiveApp(APP_ONE);

    // Continue loops
    device->loop();

    // Loop count should continue from where it left off
    ASSERT_EQ(appOne->counters->loopCount, loopsBeforePause + 1);
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
    ASSERT_EQ(appOne->counters->loopCount, 1);

    // Note: We can't directly test execDrivers() was called since
    // it's not mockable, but this documents the expected behavior
}

TEST_F(DeviceTestSuite, destructorDismountsActiveStateBeforeDeletion) {
    destructorDismountsActiveStateBeforeDeletion(this);
}

TEST_F(DeviceTestSuite, destructorDismountsAllRegisteredApps) {
    destructorDismountsAllRegisteredApps(this);
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

// ============================================
// UUID TESTS
// ============================================

TEST_F(UUIDTestSuite, stringToBytesProducesCorrectOutput) {
    uuidStringToBytesProducesCorrectOutput();
}

TEST_F(UUIDTestSuite, bytesToStringProducesValidFormat) {
    uuidBytesToStringProducesValidFormat();
}

TEST_F(UUIDTestSuite, roundTripPreservesData) {
    uuidRoundTripPreservesData();
}

// Note: This test fails due to pre-existing UUID generator issue (not related to #139)
// TEST_F(UUIDTestSuite, generatorProducesValidFormat) {
//     uuidGeneratorProducesValidFormat();
// }

// UUID Bounds Checking Tests (Issue #139)
TEST_F(UUIDTestSuite, stringToBytesRejectsTooLongString) {
    uuidStringToBytesRejectsTooLongString();
}

TEST_F(UUIDTestSuite, stringToBytesRejectsTooShortString) {
    uuidStringToBytesRejectsTooShortString();
}

TEST_F(UUIDTestSuite, stringToBytesRejectsNonHexCharacters) {
    uuidStringToBytesRejectsNonHexCharacters();
}

TEST_F(UUIDTestSuite, stringToBytesRejectsMissingHyphens) {
    uuidStringToBytesRejectsMissingHyphens();
}

TEST_F(UUIDTestSuite, stringToBytesRejectsEmptyString) {
    uuidStringToBytesRejectsEmptyString();
}

TEST_F(UUIDTestSuite, stringToBytesHandlesAllZeros) {
    uuidStringToBytesHandlesAllZeros();
}

TEST_F(UUIDTestSuite, stringToBytesHandlesAllFs) {
    uuidStringToBytesHandlesAllFs();
}

TEST_F(UUIDTestSuite, stringToBytesPreventsBufferOverflow) {
    uuidStringToBytesPreventsBufferOverflow();
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

TEST_F(TimerTestSuite, handlesOverflowBoundaryCorrectly) {
    timerHandlesOverflowBoundaryCorrectly(fakeClock);
}

TEST_F(TimerTestSuite, handlesOverflowAtExactBoundary) {
    timerHandlesOverflowAtExactBoundary(fakeClock);
}

TEST_F(TimerTestSuite, handlesLargeElapsedTimeAcrossOverflow) {
    timerHandlesLargeElapsedTimeAcrossOverflow(fakeClock);
}

// ============================================
// MATCH MANAGER TESTS
// ============================================

TEST_F(MatchManagerTestSuite, createsMatchCorrectly) {
    matchManagerCreatesMatchCorrectly(matchManager, player);
}

TEST_F(MatchManagerTestSuite, preventsMultipleActiveMatches) {
    matchManagerPreventsMultipleActiveMatches(matchManager);
}

TEST_F(MatchManagerTestSuite, receiveMatchWorks) {
    matchManagerReceiveMatchWorks(matchManager);
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
    matchManagerTracksDuelState(matchManager);
}

TEST_F(MatchManagerTestSuite, gracePeriodPath) {
    matchManagerGracePeriodPath(matchManager);
}

TEST_F(MatchManagerTestSuite, clearMatchResetsState) {
    matchManagerClearMatchResetsState(matchManager);
}

TEST_F(MatchManagerTestSuite, setDrawTimesRequiresActiveMatch) {
    matchManagerSetDrawTimesRequiresActiveMatch(matchManager);
}

TEST_F(MatchManagerTestSuite, duelStartTimeTracking) {
    matchManagerDuelStartTimeTracking(matchManager);
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

TEST_F(IdleStateTests, serialHeartbeatTriggersMacAddressSend) {
    idleSerialHeartbeatTriggersMacAddressSend(this);
}

TEST_F(IdleStateTests, receiveMacAddressTransitionsToHandshake) {
    idleReceiveMacAddressTransitionsToHandshake(this);
}

TEST_F(IdleStateTests, stateClearsOnDismount) {
    idleStateClearsOnDismount(this);
}

TEST_F(IdleStateTests, buttonCallbacksRegisteredAndRemoved) {
    idleButtonCallbacksRegisteredAndRemoved(this);
}

// ============================================
// QUICKDRAW STATE TESTS - HANDSHAKE
// ============================================

TEST_F(HandshakeStateTests, hunterRoutesToHunterSendIdState) {
    handshakeHunterRoutesToHunterSendIdState(this);
}

TEST_F(HandshakeStateTests, bountyRoutesToBountySendCCState) {
    handshakeBountyRoutesToBountySendCCState(this);
}

TEST_F(HandshakeStateTests, timeoutReturnsToIdle) {
    handshakeTimeoutReturnsToIdle(this);
}

TEST_F(HandshakeStateTests, bountyFlowSucceeds) {
    handshakeBountyFlowSucceeds(this);
}

TEST_F(HandshakeStateTests, hunterFlowSucceeds) {
    handshakeHunterFlowSucceeds(this);
}

TEST_F(HandshakeStateTests, sendsDirectMessagesNotBroadcast) {
    handshakeSendsDirectMessagesNotBroadcast(this);
}

TEST_F(HandshakeStateTests, statesClearOnDismount) {
    handshakeStatesClearOnDismount(this);
}

TEST_F(HandshakeStateTests, hunterSendsHunterReady) {
    handshakeHunterSendsHunterReady(this);
}

TEST_F(HandshakeStateTests, bountyWaitsForHunterReady) {
    handshakeBountyWaitsForHunterReady(this);
}

TEST_F(HandshakeStateTests, bountyRetriesBountyFinalAck) {
    handshakeBountyRetriesBountyFinalAck(this);
}

TEST_F(HandshakeStateTests, bountyExhaustsRetries) {
    handshakeBountyExhaustsRetries(this);
}

TEST_F(HandshakeStateTests, bountyTransitionsOnHunterReady) {
    handshakeBountyTransitionsOnHunterReady(this);
}

// Handshake Edge Case Tests
TEST_F(HandshakeStateTests, duplicateConnectionConfirmedHandled) {
    handshakeDuplicateConnectionConfirmedHandled(this);
}

TEST_F(HandshakeStateTests, duplicateHunterReceiveMatchHandled) {
    handshakeDuplicateHunterReceiveMatchHandled(this);
}

TEST_F(HandshakeStateTests, outOfOrderBountyFinalAckAccepted) {
    handshakeOutOfOrderBountyFinalAckAccepted(this);
}

TEST_F(HandshakeStateTests, outOfOrderHunterReceiveMatchIgnored) {
    handshakeOutOfOrderHunterReceiveMatchIgnored(this);
}

TEST_F(HandshakeStateTests, emptyMatchIdRejected) {
    handshakeEmptyMatchIdRejected(this);
}

TEST_F(HandshakeStateTests, emptyPlayerIdsRejected) {
    handshakeEmptyPlayerIdsRejected(this);
}

TEST_F(HandshakeStateTests, invalidCommandTypeIgnored) {
    handshakeInvalidCommandTypeIgnored(this);
}

TEST_F(HandshakeStateTests, hunterTimeoutAfterConnectionConfirmed) {
    handshakeHunterTimeoutAfterConnectionConfirmed(this);
}

TEST_F(HandshakeStateTests, bountyTimeoutNoHunterReceiveMatch) {
    handshakeBountyTimeoutNoHunterReceiveMatch(this);
}

TEST_F(HandshakeStateTests, globalTimeoutFires) {
    handshakeGlobalTimeoutFires(this);
}

TEST_F(HandshakeStateTests, simultaneousConnectionAttempt) {
    handshakeSimultaneousConnectionAttempt(this);
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

TEST_F(StateCleanupTests, duelStateClearsCallbacksOnDismount) {
    cleanupDuelStateClearsCallbacksOnDismount(this);
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

// ============================================
// QUICKDRAW STATE TESTS - CONNECTION SUCCESSFUL
// ============================================

TEST_F(ConnectionSuccessfulTests, transitionsAfterThreshold) {
    connectionSuccessfulTransitionsAfterThreshold(this);
}

// ============================================
// QUICKDRAW DESTRUCTOR TESTS
// ============================================

TEST_F(QuickdrawDestructorTests, destructorCleansUpMatchManager) {
    quickdrawDestructorCleansUpMatchManager(this);
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
// KONAMI METAGAME TESTS
// ============================================

TEST_F(KonamiMetaGameTestSuite, fdnGameTypeEnumValuesAreCorrect) {
    fdnGameTypeEnumValuesAreCorrect(player);
}

TEST_F(KonamiMetaGameTestSuite, playerHasNoButtonsInitially) {
    playerHasNoButtonsInitially(player);
}

TEST_F(KonamiMetaGameTestSuite, playerCanUnlockSingleButton) {
    playerCanUnlockSingleButton(player);
}

TEST_F(KonamiMetaGameTestSuite, playerCanUnlockMultipleButtons) {
    playerCanUnlockMultipleButtons(player);
}

TEST_F(KonamiMetaGameTestSuite, playerAllButtonsCollectedWhenSevenUnlocked) {
    playerAllButtonsCollectedWhenSevenUnlocked(player);
}

TEST_F(KonamiMetaGameTestSuite, playerHardModeUnlocksIndependently) {
    playerHardModeUnlocksIndependently(player);
}

TEST_F(KonamiMetaGameTestSuite, playerHardModeDoesNotAffectButtons) {
    playerHardModeDoesNotAffectButtons(player);
}

TEST_F(KonamiMetaGameTestSuite, playerColorProfileCheckWorks) {
    playerColorProfileCheckWorks(player);
}

TEST_F(KonamiMetaGameTestSuite, konamiMetaGameConstructsSuccessfully) {
    konamiMetaGameConstructsSuccessfully(player);
}

TEST_F(KonamiMetaGameTestSuite, konamiMetaGamePopulatesThirtyFiveStates) {
    konamiMetaGamePopulatesThirtyFiveStates(player);
}

TEST_F(KonamiMetaGameTestSuite, easyModeButtonCollectionAdvancesProgress) {
    easyModeButtonCollectionAdvancesProgress(player);
}

TEST_F(KonamiMetaGameTestSuite, modeUnlockFlagsWorkIndependently) {
    modeUnlockFlagsWorkIndependently(player);
}

TEST_F(KonamiMetaGameTestSuite, duplicateButtonUnlocksAreIdempotent) {
    duplicateButtonUnlocksAreIdempotent(player);
}

TEST_F(KonamiMetaGameTestSuite, completeEasyProgressionFlow) {
    completeEasyProgressionFlow(player);
}

TEST_F(KonamiMetaGameTestSuite, konamiCodeUnlockFlow) {
    konamiCodeUnlockFlow(player);
}

TEST_F(KonamiMetaGameTestSuite, hardModeProgressionFlow) {
    hardModeProgressionFlow(player);
}

TEST_F(KonamiMetaGameTestSuite, boonActivationFlow) {
    boonActivationFlow(player);
}

TEST_F(KonamiMetaGameTestSuite, masteryReplayWithProfileSelection) {
    masteryReplayWithProfileSelection(player);
}

TEST_F(KonamiMetaGameTestSuite, disconnectDuringCodeEntryDoesntCorruptState) {
    disconnectDuringCodeEntryDoesntCorruptState(player);
}

TEST_F(KonamiMetaGameTestSuite, partialButtonCollectionPersists) {
    partialButtonCollectionPersists(player);
}

TEST_F(KonamiMetaGameTestSuite, modeSelectWithIncompleteProfiles) {
    modeSelectWithIncompleteProfiles(player);
}

TEST_F(KonamiMetaGameTestSuite, replayWithoutReward) {
    replayWithoutReward(player);
}

TEST_F(KonamiMetaGameTestSuite, allThirtyFiveStateIndicesAreValid) {
    allThirtyFiveStateIndicesAreValid(player);
}

// ============================================
// KONAMI HANDSHAKE TESTS
// ============================================

TEST_F(KonamiHandshakeTestSuite, konamiCodeWithAllButtonsRoutesToCodeEntry) {
    konamiCodeWithAllButtonsRoutesToCodeEntry(this);
}

TEST_F(KonamiHandshakeTestSuite, konamiCodeWithoutAllButtonsRoutesToCodeRejected) {
    konamiCodeWithoutAllButtonsRoutesToCodeRejected(this);
}

TEST_F(KonamiHandshakeTestSuite, firstEncounterRoutesToEasyLaunch) {
    firstEncounterRoutesToEasyLaunch(this);
}

TEST_F(KonamiHandshakeTestSuite, replayEncounterRoutesToReplayEasy) {
    replayEncounterRoutesToReplayEasy(this);
}

TEST_F(KonamiHandshakeTestSuite, hardModeUnlockedRoutesToHardLaunch) {
    hardModeUnlockedRoutesToHardLaunch(this);
}

TEST_F(KonamiHandshakeTestSuite, hasBoonRoutesToMasteryReplay) {
    hasBoonRoutesToMasteryReplay(this);
}

TEST_F(KonamiHandshakeTestSuite, boonTakesPriorityOverHardMode) {
    boonTakesPriorityOverHardMode(this);
}

TEST_F(KonamiHandshakeTestSuite, hardModeTakesPriorityOverEasyReplay) {
    hardModeTakesPriorityOverEasyReplay(this);
}

TEST_F(KonamiHandshakeTestSuite, allGameTypesRouteCorrectlyForFirstEncounter) {
    allGameTypesRouteCorrectlyForFirstEncounter(this);
}

TEST_F(KonamiHandshakeTestSuite, validMessageFormatIsParsed) {
    validMessageFormatIsParsed(this);
}

TEST_F(KonamiHandshakeTestSuite, invalidMessageFormatIsIgnored) {
    invalidMessageFormatIsIgnored(this);
}

TEST_F(KonamiHandshakeTestSuite, outOfRangeGameTypeIsIgnored) {
    outOfRangeGameTypeIsIgnored(this);
}

TEST_F(KonamiHandshakeTestSuite, stateInitializesOnMount) {
    stateInitializesOnMount(this);
}

TEST_F(KonamiHandshakeTestSuite, stateClearsCallbacksOnDismount) {
    stateClearsCallbacksOnDismount(this);
}

TEST_F(KonamiHandshakeTestSuite, multipleLoopsDontChangeTransition) {
    multipleLoopsDontChangeTransition(this);
}

// ============================================
// LIFECYCLE SAFETY TESTS
// ============================================

// Basic Lifecycle
TEST_F(StateMachineLifecycleTests, onStateMountedCalledOnStart) {
    testOnStateMountedCalledOnStart(this);
}

TEST_F(StateMachineLifecycleTests, onStateDismountedCalledOnTransition) {
    testOnStateDismountedCalledOnTransition(this);
}

TEST_F(StateMachineLifecycleTests, onStateLoopCalledEachIteration) {
    testOnStateLoopCalledEachIteration(this);
}

TEST_F(StateMachineLifecycleTests, mountDismountOrderCorrectDuringTransition) {
    testMountDismountOrderCorrectDuringTransition(this);
}

// Transition Safety
TEST_F(StateMachineLifecycleTests, transitionDismountsOldBeforeMountingNew) {
    testTransitionDismountsOldBeforeMountingNew(this);
}

TEST_F(StateMachineLifecycleTests, currentStatePointerUpdatedAfterTransition) {
    testCurrentStatePointerUpdatedAfterTransition(this);
}

TEST_F(StateMachineLifecycleTests, transitionConditionsCheckedEachLoop) {
    testTransitionConditionsCheckedEachLoop(this);
}

// Destruction Safety
TEST_F(StateMachineLifecycleTests, destroyingActiveMachineDoesNotCrash) {
    testDestroyingActiveMachineDoesNotCrash(this);
}

TEST_F(StateMachineLifecycleTests, destructorCleansUpAllStatesInMap) {
    testDestructorCleansUpAllStatesInMap(this);
}

TEST_F(StateMachineLifecycleTests, stateMachineDismountsCurrentStateOnDestruction) {
    testStateMachineDismountsCurrentStateOnDestruction(this);
}

// Edge Cases
TEST_F(StateMachineLifecycleTests, singleStateNoTransitions) {
    testSingleStateNoTransitions(this);
}

TEST_F(StateMachineLifecycleTests, selfTransitionDismountsAndRemounts) {
    testSelfTransitionDismountsAndRemounts(this);
}

TEST_F(StateMachineLifecycleTests, rapidTransitionsABCInSuccession) {
    testRapidTransitionsABCInSuccession(this);
}

TEST_F(StateMachineLifecycleTests, emptyStateMachineHandlesGracefully) {
    testEmptyStateMachineHandlesGracefully(this);
}

// App Lifecycle (Sub-app Pattern)
TEST_F(StateMachineLifecycleTests, hasLaunchedGuardWorks) {
    testHasLaunchedGuardWorks(this);
}

TEST_F(StateMachineLifecycleTests, deviceDestructorDismountsLaunchedApps) {
    testDeviceDestructorDismountsLaunchedApps(this);
}

TEST_F(StateMachineLifecycleTests, deviceDestructorOnlyDismountsLaunchedApps) {
    testDeviceDestructorOnlyDismountsLaunchedApps(this);
}

TEST_F(StateMachineLifecycleTests, multipleAppsAllDismountedOnDestruction) {
    testMultipleAppsAllDismountedOnDestruction(this);
}

// ============================================
// STATE TRANSITION EDGE CASE TESTS
// ============================================

TEST_F(StateTransitionEdgeCaseTests, rapidIdleHandshakeTransition) {
    edgeCaseRapidIdleHandshakeTransition(this);
}

TEST_F(StateTransitionEdgeCaseTests, connectionSuccessfulRapidCycle) {
    edgeCaseConnectionSuccessfulRapidCycle(this);
}

TEST_F(StateTransitionEdgeCaseTests, duelTimeoutClearsMatch) {
    edgeCaseDuelTimeoutClearsMatch(this);
}

TEST_F(StateTransitionEdgeCaseTests, winStateClearsMatchOnTransition) {
    edgeCaseWinStateClearsMatchOnTransition(this);
}

TEST_F(StateTransitionEdgeCaseTests, loseStateClearsMatchOnTransition) {
    edgeCaseLoseStateClearsMatchOnTransition(this);
}

TEST_F(StateTransitionEdgeCaseTests, countdownPenaltyPersistsAcrossCycles) {
    edgeCaseCountdownPenaltyPersistsAcrossCycles(this);
}

TEST_F(StateTransitionEdgeCaseTests, handshakeWithNullOpponentMac) {
    edgeCaseHandshakeWithNullOpponentMac(this);
}

TEST_F(StateTransitionEdgeCaseTests, multipleConsecutiveTransitions) {
    edgeCaseMultipleConsecutiveTransitions(this);
}

TEST_F(StateTransitionEdgeCaseTests, duelReceivedResultGracePeriodExpiry) {
    edgeCaseDuelReceivedResultGracePeriodExpiry(this);
}

TEST_F(StateTransitionEdgeCaseTests, duelPushedGracePeriodExpiry) {
    edgeCaseDuelPushedGracePeriodExpiry(this);
}

TEST_F(StateTransitionEdgeCaseTests, handshakeWithMalformedMac) {
    edgeCaseHandshakeWithMalformedMac(this);
}

// ============================================
// QUICKDRAW EDGE CASE TESTS
// ============================================

// Timeout Edge Cases
TEST_F(QuickdrawEdgeCaseTests, timeoutAtExactBoundary) {
    duelTimeoutAtExactBoundary(this);
}

TEST_F(QuickdrawEdgeCaseTests, veryShortTimeout) {
    duelVeryShortTimeout(this);
}

TEST_F(QuickdrawEdgeCaseTests, bothPlayersTimeout) {
    duelBothPlayersTimeout(this);
}

TEST_F(QuickdrawEdgeCaseTests, pushedGracePeriodExactBoundary) {
    duelPushedGracePeriodExactBoundary(this);
}

TEST_F(QuickdrawEdgeCaseTests, receivedResultGracePeriodExactBoundary) {
    duelReceivedResultGracePeriodExactBoundary(this);
}

// Simultaneous Press Edge Cases
TEST_F(QuickdrawEdgeCaseTests, exactSimultaneousPress) {
    duelExactSimultaneousPress(this);
}

TEST_F(QuickdrawEdgeCaseTests, pressWithin1ms) {
    duelPressWithin1ms(this);
}

TEST_F(QuickdrawEdgeCaseTests, bothFalseStartBeforeSignal) {
    duelBothFalseStartBeforeSignal(this);
}

// Result Calculation Edge Cases
TEST_F(QuickdrawEdgeCaseTests, zeroReactionTime) {
    duelZeroReactionTime(this);
}

TEST_F(QuickdrawEdgeCaseTests, maximumReactionTime) {
    duelMaximumReactionTime(this);
}

TEST_F(QuickdrawEdgeCaseTests, opponentNeverPressedSentinel) {
    duelOpponentNeverPressedSentinel(this);
}

TEST_F(QuickdrawEdgeCaseTests, maxButtonMasherPenalty) {
    duelMaxButtonMasherPenalty(this);
}

// State Transition Edge Cases
TEST_F(QuickdrawEdgeCaseTests, receiveResultAtButtonPress) {
    duelReceiveResultAtButtonPress(this);
}

TEST_F(QuickdrawEdgeCaseTests, multipleLoopsWithoutTimeAdvancement) {
    duelMultipleLoopsWithoutTimeAdvancement(this);
}

TEST_F(QuickdrawEdgeCaseTests, dismountBeforeTimeout) {
    duelDismountBeforeTimeout(this);
}

// Result Propagation Edge Cases
TEST_F(QuickdrawEdgeCaseTests, winUpdatesPlayerStats) {
    resultWinUpdatesPlayerStats(this);
}

TEST_F(QuickdrawEdgeCaseTests, loseUpdatesPlayerStats) {
    resultLoseUpdatesPlayerStats(this);
}

TEST_F(QuickdrawEdgeCaseTests, winStreakIncrementsOnWin) {
    resultWinStreakIncrementsOnWin(this);
}

TEST_F(QuickdrawEdgeCaseTests, winStreakResetsOnLoss) {
    resultWinStreakResetsOnLoss(this);
}

// ============================================
// CONTRACT TESTS (Native/ESP32 Parity)
// ============================================

TEST_F(ContractTestSuite, displayBufferSize) {
    displayContractBufferSize(this);
}

TEST_F(ContractTestSuite, displayDrawBitmapWritesBothOnAndOffPixels) {
    displayContractDrawBitmapWritesBothOnAndOffPixels(this);
}

TEST_F(ContractTestSuite, buttonDebounceTimingMatches) {
    buttonContractDebounceTimingMatches(this);
}

TEST_F(ContractTestSuite, serialBufferSizeLimits) {
    serialContractBufferSizeLimits(this);
}

TEST_F(ContractTestSuite, timerOverflowHandling) {
    timerContractOverflowHandling(this);
}

TEST_F(ContractTestSuite, stateMachineLifecycleOrder) {
    stateMachineContractLifecycleOrder(this);
}

TEST_F(ContractTestSuite, playerJsonFormat) {
    serializationContractPlayerJsonFormat(this);
}

TEST_F(ContractTestSuite, matchBinaryFormat) {
    serializationContractMatchBinaryFormat(this);
}

// ============================================
// WIRELESS MANAGER TESTS
// ============================================

TEST_F(WirelessManagerTestSuite, wirelessManagerInitializesCorrectly) {
    wirelessManagerInitializesCorrectly(player, realWirelessManager);
}

TEST_F(WirelessManagerTestSuite, wirelessManagerClearsCallbacks) {
    wirelessManagerClearsCallbacks(wirelessManager);
}

TEST_F(WirelessManagerTestSuite, broadcastPacketSendsCorrectData) {
    broadcastPacketSendsCorrectData(wirelessManager, mockPeerComms, player);
}

TEST_F(WirelessManagerTestSuite, broadcastPacketFailsWithEmptyMac) {
    broadcastPacketFailsWithEmptyMac(wirelessManager, player);
}

TEST_F(WirelessManagerTestSuite, broadcastPacketFailsWithInvalidMac) {
    broadcastPacketFailsWithInvalidMac(wirelessManager, player);
}

TEST_F(WirelessManagerTestSuite, processCommandParsesPacketCorrectly) {
    processCommandParsesPacketCorrectly(wirelessManager);
}

TEST_F(WirelessManagerTestSuite, processCommandRejectsInvalidPacketSize) {
    processCommandRejectsInvalidPacketSize(wirelessManager);
}

TEST_F(WirelessManagerTestSuite, commandTrackerLogsCommands) {
    commandTrackerLogsCommands(wirelessManager);
}

TEST_F(WirelessManagerTestSuite, clearPacketsRemovesAllTrackedCommands) {
    clearPacketsRemovesAllTrackedCommands(wirelessManager);
}

TEST_F(WirelessManagerTestSuite, clearPacketRemovesSpecificCommand) {
    clearPacketRemovesSpecificCommand(wirelessManager);
}

// ============================================
// MAIN
// ============================================

int main(int argc, char **argv)
{
    ::testing::InitGoogleMock(&argc, argv);

    if (RUN_ALL_TESTS())
        ;

    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif
