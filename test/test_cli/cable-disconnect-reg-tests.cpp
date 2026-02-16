//
// Cable Disconnect Test Registrations
//

#include "cable-disconnect-tests.hpp"

// ============================================
// TEST REGISTRATIONS
// ============================================

TEST_F(CableDisconnectTestSuite, CableDisconnectDuringIntro) {
    cableDisconnectDuringIntro(this);
}

TEST_F(CableDisconnectTestSuite, CableDisconnectDuringGameplay) {
    cableDisconnectDuringGameplay(this);
}

TEST_F(CableDisconnectTestSuite, CableReconnectToDifferentNpc) {
    cableReconnectToDifferentNpc(this);
}
