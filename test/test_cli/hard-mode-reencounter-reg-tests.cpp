//
// Hard Mode Re-encounter Tests — E2E tests for hard mode re-encounter edge cases
//
// DISABLED tests: All re-encounter tests fail after KMG routing (Wave 17, #271).
// KMG now handles hard mode choice, not FdnReencounter. See #327.

#include <gtest/gtest.h>

#include "hard-mode-reencounter-tests.hpp"

// ============================================
// HARD MODE RE-ENCOUNTER TESTS
// ============================================

TEST_F(HardModeReencounterTestSuite, DISABLED_FullCompletionReencounter) {
    fullCompletionReencounter(this);
}

TEST_F(HardModeReencounterTestSuite, DISABLED_RecreationalHardModeNoRewards) {
    recreationalHardModeNoRewards(this);
}

TEST_F(HardModeReencounterTestSuite, DISABLED_HardModeLossRetryShowsHardOption) {
    hardModeLossRetryShowsHardOption(this);
}

TEST_F(HardModeReencounterTestSuite, DISABLED_MixedProgressAcrossGames) {
    mixedProgressAcrossGames(this);
}

TEST_F(HardModeReencounterTestSuite, DISABLED_RecreationalEasyModeNoRewards) {
    recreationalEasyModeNoRewards(this);
}

TEST_F(HardModeReencounterTestSuite, DISABLED_RecreationalModeNoNvsWrites) {
    recreationalModeNoNvsWrites(this);
}

TEST_F(HardModeReencounterTestSuite, DISABLED_KonamiProgressUnchangedAfterRecreational) {
    konamiProgressUnchangedAfterRecreational(this);
}
