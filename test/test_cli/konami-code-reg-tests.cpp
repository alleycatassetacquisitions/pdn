#ifdef NATIVE_BUILD

#include "konami-code-tests.hpp"

// ============================================
// KONAMI CODE TESTS - Registration
// ============================================

TEST_F(KonamiCodeTestSuite, CorrectSequenceCompletesSuccessfully) {
    testKonamiCodeCorrectSequence(this);
}

TEST_F(KonamiCodeTestSuite, IncorrectInputResetsSequence) {
    testKonamiCodeIncorrectInput(this);
}

TEST_F(KonamiCodeTestSuite, InactivityTimeoutTriggersGameOver) {
    testKonamiCodeTimeout(this);
}

TEST_F(KonamiCodeTestSuite, RejectedStateWithNoButtons) {
    testKonamiCodeRejectedNoButtons(this);
}

TEST_F(KonamiCodeTestSuite, AcceptedStateUnlocksHardMode) {
    testKonamiCodeAcceptedStateTransition(this);
}

TEST_F(KonamiCodeTestSuite, GameOverReturnsToIdle) {
    testGameOverReturnIdleTransition(this);
}

TEST_F(KonamiCodeTestSuite, HardModeUnlockPersists) {
    testHardModeUnlockPersistence(this);
}

#endif  // NATIVE_BUILD
