//
// Hard Mode Re-encounter Tests — E2E tests for hard mode re-encounter edge cases
//

#include <gtest/gtest.h>

#include "hard-mode-reencounter-tests.hpp"

// ============================================
// HARD MODE RE-ENCOUNTER TESTS
// ============================================

TEST_F(HardModeReencounterTestSuite, FullCompletionReencounter) {
    fullCompletionReencounter(this);
}

TEST_F(HardModeReencounterTestSuite, RecreationalHardModeNoRewards) {
    recreationalHardModeNoRewards(this);
}

TEST_F(HardModeReencounterTestSuite, HardModeLossRetryShowsHardOption) {
    hardModeLossRetryShowsHardOption(this);
}

TEST_F(HardModeReencounterTestSuite, MixedProgressAcrossGames) {
    mixedProgressAcrossGames(this);
}

TEST_F(HardModeReencounterTestSuite, RecreationalEasyModeNoRewards) {
    recreationalEasyModeNoRewards(this);
}

TEST_F(HardModeReencounterTestSuite, RecreationalModeNoNvsWrites) {
    recreationalModeNoNvsWrites(this);
}

TEST_F(HardModeReencounterTestSuite, KonamiProgressUnchangedAfterRecreational) {
    konamiProgressUnchangedAfterRecreational(this);
}
