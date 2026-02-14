//
// Konami Button Awarded Tests — Registration file for KonamiButtonAwarded and GameOverReturnIdle state tests
//

#include <gtest/gtest.h>

#include "konami-button-awarded-tests.hpp"

// ============================================
// KONAMI BUTTON AWARDED TESTS
// ============================================

TEST_F(KonamiButtonAwardedTestSuite, ButtonPersistenceAfterAward) {
    buttonPersistenceAfterAward(this);
}

TEST_F(KonamiButtonAwardedTestSuite, KonamiProgressAfterMultipleButtons) {
    konamiProgressAfterMultipleButtons(this);
}

TEST_F(KonamiButtonAwardedTestSuite, GameOverReturnIdleCleanup) {
    gameOverReturnIdleCleanup(this);
}

TEST_F(KonamiButtonAwardedTestSuite, DisconnectNoSideEffects) {
    disconnectNoSideEffects(this);
}

TEST_F(KonamiButtonAwardedTestSuite, VictoryDisplayRendersCorrectly) {
    victoryDisplayRendersCorrectly(this);
}

TEST_F(KonamiButtonAwardedTestSuite, DuplicateButtonAwardNoDoubleCount) {
    duplicateButtonAwardNoDoubleCount(this);
}
