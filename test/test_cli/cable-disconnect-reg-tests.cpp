//
// Cable Disconnect Test Registrations
//

#include "cable-disconnect-tests.hpp"

// ============================================
// TEST REGISTRATIONS
// ============================================

// Cable disconnect detection implemented in PR #XXX (fixes Issue #207)
// Tests verify that minigames abort when player cable is disconnected.

TEST_F(CableDisconnectTestSuite, CableDisconnectDuringIntro) {
    cableDisconnectDuringIntro(this);
}

TEST_F(CableDisconnectTestSuite, CableDisconnectDuringGameplay) {
    cableDisconnectDuringGameplay(this);
}

TEST_F(CableDisconnectTestSuite, CableReconnectToDifferentNpc) {
    cableReconnectToDifferentNpc(this);
}
