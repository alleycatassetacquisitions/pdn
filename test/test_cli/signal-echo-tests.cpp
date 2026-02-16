//
// Signal Echo Tests — Signal Echo gameplay + difficulty
//

#include <gtest/gtest.h>

#include "signal-echo-tests.hpp"

// ============================================
// SIGNAL ECHO TESTS
// ============================================

TEST_F(SignalEchoTestSuite, SequenceGenerationLength) {
    echoSequenceGenerationLength(this);
}

TEST_F(SignalEchoTestSuite, SequenceGenerationMixed) {
    echoSequenceGenerationMixed(this);
}

TEST_F(SignalEchoTestSuite, IntroTransitionsToShow) {
    echoIntroTransitionsToShow(this);
}

TEST_F(SignalEchoTestSuite, ShowSequenceDisplaysSignals) {
    echoShowSequenceDisplaysSignals(this);
}

TEST_F(SignalEchoTestSuite, ShowTransitionsToInput) {
    echoShowTransitionsToInput(this);
}

TEST_F(SignalEchoTestSuite, CorrectInputAdvancesIndex) {
    echoCorrectInputAdvancesIndex(this);
}

TEST_F(SignalEchoTestSuite, WrongInputStored) {
    echoWrongInputStored(this);
}

TEST_F(SignalEchoTestSuite, AllCorrectInputsNextRound) {
    echoAllCorrectInputsNextRound(this);
}

TEST_F(SignalEchoTestSuite, WrongSequenceLoses) {
    echoWrongSequenceLoses(this);
}

TEST_F(SignalEchoTestSuite, AllRoundsCompletedWin) {
    echoAllRoundsCompletedWin(this);
}

TEST_F(SignalEchoTestSuite, CumulativeModeAppends) {
    echoCumulativeModeAppends(this);
}

TEST_F(SignalEchoTestSuite, FreshModeNewSequence) {
    echoFreshModeNewSequence(this);
}

TEST_F(SignalEchoTestSuite, WinSetsOutcome) {
    echoWinSetsOutcome(this);
}

TEST_F(SignalEchoTestSuite, LoseSetsOutcome) {
    echoLoseSetsOutcome(this);
}

TEST_F(SignalEchoTestSuite, IsGameCompleteAfterWin) {
    echoIsGameCompleteAfterWin(this);
}

TEST_F(SignalEchoTestSuite, ResetGameClearsOutcome) {
    echoResetGameClearsOutcome(this);
}

TEST_F(SignalEchoTestSuite, StandaloneRestartAfterWin) {
    echoStandaloneRestartAfterWin(this);
}

// ============================================
// SIGNAL ECHO DIFFICULTY TESTS
// ============================================

TEST_F(SignalEchoDifficultyTestSuite, EasySequenceLength) {
    echoDiffEasySequenceLength(this);
}

TEST_F(SignalEchoDifficultyTestSuite, EasyConfigParams) {
    echoDiffEasyConfigParams(this);
}

TEST_F(SignalEchoDifficultyTestSuite, HardSequenceLength) {
    echoDiffHardSequenceLength(this);
}

TEST_F(SignalEchoDifficultyTestSuite, WrongSequenceLoses) {
    echoDiffWrongSequenceLoses(this);
}

TEST_F(SignalEchoDifficultyTestSuite, EasyWinOutcome) {
    echoDiffEasyWinOutcome(this);
}

TEST_F(SignalEchoDifficultyTestSuite, HardWinOutcome) {
    echoDiffHardWinOutcome(this);
}

TEST_F(SignalEchoDifficultyTestSuite, LifeIndicator) {
    echoDiffLifeIndicator(this);
}

TEST_F(SignalEchoDifficultyTestSuite, WrongInputAdvances) {
    echoDiffWrongInputAdvances(this);
}

// ============================================
// EDGE CASE TESTS (NEW)
// ============================================

TEST_F(SignalEchoTestSuite, OneWrongArrowLoses) {
    echoOneWrongArrowLoses(this);
}

TEST_F(SignalEchoTestSuite, ButtonPressDuringIntroIgnored) {
    echoButtonPressDuringIntroIgnored(this);
}

TEST_F(SignalEchoTestSuite, ButtonPressDuringShowIgnored) {
    echoButtonPressDuringShowIgnored(this);
}

TEST_F(SignalEchoTestSuite, CumulativeModeMaxLength) {
    echoCumulativeModeMaxLength(this);
}

TEST_F(SignalEchoTestSuite, ZeroLengthSequenceConfig) {
    echoZeroLengthSequenceConfig(this);
}
