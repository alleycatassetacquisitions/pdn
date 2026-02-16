//
// Mastery Replay Tests — Registration file for MasteryReplay state
//

#include <gtest/gtest.h>

#include "mastery-replay-tests.hpp"

// ============================================
// MASTERY REPLAY TESTS
// ============================================

TEST_F(MasteryReplayTestSuite, UpCyclesModes) {
    masteryReplayUpCyclesModes(this);
}

TEST_F(MasteryReplayTestSuite, DISABLED_DownTransitionsToEasy) {
    masteryReplayDownTransitionsToEasy(this);
}

TEST_F(MasteryReplayTestSuite, DISABLED_DownTransitionsToHard) {
    masteryReplayDownTransitionsToHard(this);
}

TEST_F(MasteryReplayTestSuite, DisplaysCorrectGameName) {
    masteryReplayDisplaysCorrectGameName(this);
}

TEST_F(MasteryReplayTestSuite, DISABLED_CleanupOnDismount) {
    masteryReplayCleanupOnDismount(this);
}

TEST_F(MasteryReplayTestSuite, CorrectStateId) {
    masteryReplayCorrectStateId(this);
}
