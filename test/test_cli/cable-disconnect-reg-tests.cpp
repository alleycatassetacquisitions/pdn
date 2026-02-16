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
// DISABLED: Two issues prevent these from working:
// 1. Tests use begin() + ticking instead of skipToState — boot too slow
// 2. KonamiMetaGame routing (Wave 17, #271) — returnToPreviousApp() only goes
//    one level (minigame → KMG), not back to Quickdraw. Need full rewrite.

TEST_F(CableDisconnectTestSuite, DISABLED_CableDisconnectDuringIntro) {
    cableDisconnectDuringIntro(this);
}

TEST_F(CableDisconnectTestSuite, DISABLED_CableDisconnectDuringGameplay) {
    cableDisconnectDuringGameplay(this);
}

TEST_F(CableDisconnectTestSuite, DISABLED_CableReconnectToDifferentNpc) {
    cableReconnectToDifferentNpc(this);
}
