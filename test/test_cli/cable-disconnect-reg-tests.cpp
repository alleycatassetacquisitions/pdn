//
// Cable Disconnect Test Registrations
//

#include "cable-disconnect-tests.hpp"

// ============================================
// TEST REGISTRATIONS
// ============================================

// DISABLED: Cable disconnect detection not yet implemented in minigames (Issue #271)
// These tests were added prematurely in PR #254. They expect minigames to detect
// serial disconnects and return to Idle, but this feature was never completed.
// Re-enable when minigame disconnect monitoring is implemented.

TEST_F(CableDisconnectTestSuite, DISABLED_CableDisconnectDuringIntro) {
    cableDisconnectDuringIntro(this);
}

TEST_F(CableDisconnectTestSuite, DISABLED_CableDisconnectDuringGameplay) {
    cableDisconnectDuringGameplay(this);
}

TEST_F(CableDisconnectTestSuite, DISABLED_CableReconnectToDifferentNpc) {
    cableReconnectToDifferentNpc(this);
}
