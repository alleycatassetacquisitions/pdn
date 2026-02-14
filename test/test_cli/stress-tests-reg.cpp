//
// Stress Tests — Registration file for performance and stress testing
//

#include <gtest/gtest.h>

#include "stress-tests.hpp"

// ============================================
// RAPID STATE TRANSITION TESTS
// ============================================

TEST_F(StressTestSuite, RapidStateTransitions) {
    stressRapidStateTransitions(this);
}

TEST_F(MinigameStressTestSuite, FullGameLifecycles) {
    stressFullGameLifecycles(this);
}

TEST_F(StressTestSuite, StateTransitionDuringTransition) {
    stressStateTransitionDuringTransition(this);
}

// ============================================
// BUTTON SPAM TESTS
// ============================================

TEST_F(StressTestSuite, ButtonSpam) {
    stressButtonSpam(this);
}

TEST_F(StressTestSuite, AlternatingButtonSpam) {
    stressAlternatingButtonSpam(this);
}

TEST_F(StressTestSuite, SimultaneousButtonPress) {
    stressSimultaneousButtonPress(this);
}

TEST_F(StressTestSuite, ButtonsDuringTransition) {
    stressButtonsDuringTransition(this);
}

TEST_F(MinigameStressTestSuite, ButtonSpamDuringGameplay) {
    stressButtonSpamDuringGameplay(this);
}

// ============================================
// TIMER EDGE CASE TESTS
// ============================================

TEST_F(MinigameStressTestSuite, TimerButtonRaceCondition) {
    stressTimerButtonRaceCondition(this);
}

TEST_F(StressTestSuite, MultipleTimerExpiration) {
    stressMultipleTimerExpiration(this);
}

// ============================================
// MEMORY STRESS TESTS
// ============================================

TEST_F(StressTestSuite, SerialMessageFlood) {
    stressSerialMessageFlood(this);
}

TEST_F(MinigameStressTestSuite, DisplayUpdates) {
    stressDisplayUpdates(this);
}

TEST_F(StressTestSuite, ContinuousLoop) {
    stressContinuousLoop(this);
}
