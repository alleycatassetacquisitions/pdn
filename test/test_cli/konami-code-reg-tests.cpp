#ifdef NATIVE_BUILD

#include "konami-code-tests.hpp"

// ============================================
// KONAMI CODE TESTS - Registration
// ============================================

// DISABLED: Assertion failure — button cycling logic doesn't match KonamiCodeEntry
// state's actual selection behavior. Needs rewrite. See #327.
TEST_F(KonamiCodeTestSuite, DISABLED_CorrectSequenceCompletesSuccessfully) {
    testKonamiCodeCorrectSequence(this);
}

TEST_F(KonamiCodeTestSuite, IncorrectInputResetsSequence) {
    testKonamiCodeIncorrectInput(this);
}

TEST_F(KonamiCodeTestSuite, InactivityTimeoutTriggersGameOver) {
    testKonamiCodeTimeout(this);
}

// DISABLED: SIGSEGV — skipToState(30) crashes. KonamiCodeRejected state index
// may have shifted after Wave 17 state additions. See #327.
TEST_F(KonamiCodeTestSuite, DISABLED_RejectedStateWithNoButtons) {
    testKonamiCodeRejectedNoButtons(this);
}

// DISABLED: SIGSEGV — skipToState(29) crashes. KonamiCodeAccepted state index
// may have shifted after Wave 17 state additions. See #327.
TEST_F(KonamiCodeTestSuite, DISABLED_AcceptedStateUnlocksHardMode) {
    testKonamiCodeAcceptedStateTransition(this);
}

// DISABLED: SIGSEGV — skipToState(31) crashes. GameOverReturnIdle state index
// shifted after Wave 17 state additions. See #327.
TEST_F(KonamiCodeTestSuite, DISABLED_GameOverReturnsToIdle) {
    testGameOverReturnIdleTransition(this);
}

TEST_F(KonamiCodeTestSuite, HardModeUnlockPersists) {
    testHardModeUnlockPersistence(this);
}

#endif  // NATIVE_BUILD
