//
// Konami Progression E2E Tests — Registration file for Konami button progression E2E tests
//

#include <gtest/gtest.h>

#include "konami-progression-e2e-tests.hpp"

// ============================================
// KONAMI PROGRESSION E2E TESTS
// ============================================

TEST_F(KonamiProgressionE2ETestSuite, FullSevenGames) {
    konamiProgressionFullSevenGames(this);
}

TEST_F(KonamiProgressionE2ETestSuite, PartialThreeGames) {
    konamiProgressionPartialThreeGames(this);
}

TEST_F(KonamiProgressionE2ETestSuite, DuplicateWin) {
    konamiProgressionDuplicateWin(this);
}

TEST_F(KonamiProgressionE2ETestSuite, UnlockCheck) {
    konamiProgressionUnlockCheck(this);
}

TEST_F(KonamiProgressionE2ETestSuite, ButtonMapping) {
    konamiProgressionButtonMapping(this);
}

TEST_F(KonamiProgressionE2ETestSuite, Persistence) {
    konamiProgressionPersistence(this);
}
