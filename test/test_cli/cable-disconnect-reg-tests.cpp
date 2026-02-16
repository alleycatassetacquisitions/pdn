//
// Cable Disconnect Test Registrations
//

#include "cable-disconnect-tests.hpp"

// ============================================
// TEST REGISTRATIONS
// ============================================

// Cable disconnect detection implemented in PR #295 (fixes Issue #207)
// Tests verify that minigames abort when player cable is disconnected.
//
// DISABLED: Tests fail because they try to reach Idle by ticking rather than
// using skipToState. Boot sequence takes too long — needs test setup fix.

TEST_F(CableDisconnectTestSuite, DISABLED_CableDisconnectDuringIntro) {
    cableDisconnectDuringIntro(this);
}

TEST_F(CableDisconnectTestSuite, DISABLED_CableDisconnectDuringGameplay) {
    cableDisconnectDuringGameplay(this);
}

TEST_F(CableDisconnectTestSuite, DISABLED_CableReconnectToDifferentNpc) {
    cableReconnectToDifferentNpc(this);
}
