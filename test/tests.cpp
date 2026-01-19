#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Existing test headers
#include "state-machine-tests.hpp"
#include "state-tests.hpp"
#include "serial-tests.hpp"

// New test headers
#include "player-tests.hpp"
#include "match-tests.hpp"
#include "utility-tests.hpp"
#include "match-manager-tests.hpp"
#include "integration-tests.hpp"

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

    stateMachine->initialize();

    State* currentState = stateMachine->getCurrentState();

    ASSERT_TRUE(dynamic_cast<InitialTestState*>(currentState)->stateMountedInvoked);
}

TEST_F(StateMachineTestSuite, initializeAddsTransitions) {
    stateMachine->initialize();

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
    stateMachine->initialize();

    std::vector<State *> populatedStates = stateMachine->getStateMap();

    ASSERT_EQ(populatedStates.size(), 4);
}

TEST_F(StateMachineTestSuite, stateMapIsEmptyWhenStateMachineNotInitialized) {

    State* shouldBeNull = stateMachine->getCurrentState();

    ASSERT_EQ(nullptr, shouldBeNull);
}

TEST_F(StateMachineTestSuite, stateShouldTransitionAfterConditionMet) {

    stateMachine->initialize();

    advanceStateMachineToState(SECOND_STATE);

    State* currentState = stateMachine->getCurrentState();

    ASSERT_TRUE(dynamic_cast<SecondTestState*>(currentState)->stateMountedInvoked);
}

TEST_F(StateMachineTestSuite, whenTransitionIsMetStateDismounts) {

    stateMachine->initialize();

    advanceStateMachineToState(SECOND_STATE);

    State* initialState = stateMachine->getStateFromStateMap(0);

    ASSERT_TRUE(dynamic_cast<InitialTestState*>(initialState)->stateDismountedInvoked);
}

TEST_F(StateMachineTestSuite, stateMachineTransitionsThroughAllStates) {

    stateMachine->initialize();

    advanceStateMachineToState(TERMINAL_STATE);

    State* currentState = stateMachine->getCurrentState();
    ASSERT_TRUE(dynamic_cast<TerminalTestState*>(currentState)->stateMountedInvoked);
}

TEST_F(StateMachineTestSuite, stateLifecyclesAreInvoked) {
    stateMachine->initialize();

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
    stateMachine->initialize();

    advanceStateMachineToState(TERMINAL_STATE);

    int numLoopsBeforeTransition = 5;

    testing::InSequence sequence;

    EXPECT_CALL(*stateMachineDevice.mockHaptics, getIntensity())
    .Times(numLoopsBeforeTransition*2)
    .WillRepeatedly(testing::Return(0))
    .RetiresOnSaturation();

    while(numLoopsBeforeTransition--) {
        stateMachine->loop();
        ASSERT_TRUE(stateMachine->getCurrentState()->getStateId() == TERMINAL_STATE);
    }

    EXPECT_CALL(*stateMachineDevice.mockHaptics, getIntensity())
    .Times(2)
    .WillRepeatedly(testing::Return(255))
    .RetiresOnSaturation();

    stateMachine->loop();

    ASSERT_TRUE(stateMachine->getCurrentState()->getStateId() == SECOND_STATE);
}

TEST_F(StateMachineTestSuite, whenTwoTransitionsAreMetSimultaneouslyThenTheFirstTransitionAddedTriggersFirst) {
    stateMachine->initialize();

    advanceStateMachineToState(TERMINAL_STATE);

    testing::InSequence sequence;

    EXPECT_CALL(*stateMachineDevice.mockHaptics, getIntensity())
    .Times(2)
    .WillRepeatedly(testing::Return(255));

    stateMachine->loop();

    ASSERT_FALSE(stateMachine->getCurrentState()->getStateId() == INITIAL_STATE);
}

TEST_F(StateMachineTestSuite, whenCurrentStateTransitionIsValidCheckTransitionsSetsNewState) {
    stateMachine->initialize();

    InitialTestState* initial = dynamic_cast<InitialTestState*>(stateMachine->getCurrentState());

    initial->transitionToSecond = true;

    ASSERT_FALSE(stateMachine->getTransitionReadyFlag());
    ASSERT_EQ(stateMachine->getNewState(), nullptr);

    stateMachine->checkStateTransitions();

    ASSERT_TRUE(stateMachine->getTransitionReadyFlag());
    ASSERT_NE(stateMachine->getNewState(), nullptr);
}

TEST_F(StateMachineTestSuite, commitStateExecutesCorrectlyWhenNewStateIsSet) {
    stateMachine->initialize();

    InitialTestState* initial = dynamic_cast<InitialTestState*>(stateMachine->getCurrentState());

    initial->transitionToSecond = true;

    stateMachine->checkStateTransitions();

    ASSERT_TRUE(stateMachine->getTransitionReadyFlag());
    ASSERT_NE(stateMachine->getNewState(), nullptr);

    stateMachine->commitState();

    ASSERT_FALSE(stateMachine->getTransitionReadyFlag());
    ASSERT_EQ(stateMachine->getNewState(), nullptr);
    ASSERT_TRUE(stateMachine->getCurrentState()->getStateId() == SECOND_STATE);
}

// ============================================
// STATE TESTS
// ============================================

TEST_F(StateTestSuite, validMessagesAreCorrectlyReceived) {
    prepareValidMessageTest();

    readValidMessageState.onStateLoop(&stateTestDevice);

    ASSERT_TRUE(readValidMessageState.didReadValidMessage);
}

TEST_F(StateTestSuite, invalidMessagesAreDiscarded) {
    prepareInvalidMessageTest();

    readValidMessageState.onStateLoop(&stateTestDevice);

    ASSERT_FALSE(readValidMessageState.didReadValidMessage);
}

TEST_F(StateTestSuite, correctMessageIsDeliveredEvenIfGarbageInFront) {
    prepareGarbageFirstTest();

    readValidMessageState.onStateLoop(&stateTestDevice);

    ASSERT_TRUE(readValidMessageState.didReadValidMessage);
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

TEST_F(UUIDTestSuite, generatorProducesValidFormat) {
    uuidGeneratorProducesValidFormat();
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
