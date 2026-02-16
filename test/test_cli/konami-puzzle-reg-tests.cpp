//
// Konami Puzzle Tests — Registration file for Konami Puzzle meta-game
//

#include <gtest/gtest.h>

#include "konami-puzzle-tests.hpp"

// ============================================
// KONAMI PUZZLE TESTS
// ============================================

TEST_F(KonamiPuzzleTestSuite, DISABLED_RoutingWithAllButtons) {
    konamiPuzzleRoutingWithAllButtons(this);
}

TEST_F(KonamiPuzzleTestSuite, NoRoutingWithoutAllButtons) {
    konamiPuzzleNoRoutingWithoutAllButtons(this);
}

TEST_F(KonamiPuzzleTestSuite, StateMounts) {
    konamiPuzzleStateMounts(this);
}

TEST_F(KonamiPuzzleTestSuite, DISABLED_CorrectSequenceWins) {
    konamiPuzzleCorrectSequenceWins(this);
}

TEST_F(KonamiPuzzleTestSuite, WrongInputShowsError) {
    konamiPuzzleWrongInputShowsError(this);
}

TEST_F(KonamiPuzzleTestSuite, HasAllButtonsTrue) {
    hasAllKonamiButtonsTrue(this);
}

TEST_F(KonamiPuzzleTestSuite, HasAllButtonsFalse) {
    hasAllKonamiButtonsFalse(this);
}

TEST_F(KonamiPuzzleTestSuite, StateName) {
    konamiPuzzleStateName(this);
}
