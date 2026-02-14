#ifdef NATIVE_BUILD

#include "konami-metagame-e2e-tests.hpp"

// ============================================
// KONAMI METAGAME E2E TEST REGISTRATIONS
// ============================================

TEST_F(KonamiMetaGameE2ETestSuite, FirstEncounterEasyWinButtonAwarded) {
    konamiMetagameFirstEncounterEasyWinButtonAwarded(this);
}

TEST_F(KonamiMetaGameE2ETestSuite, ButtonReplayNoNewRewards) {
    konamiMetagameButtonReplayNoNewRewards(this);
}

TEST_F(KonamiMetaGameE2ETestSuite, AllButtonsKonamiCodeEntry) {
    konamiMetagameAllButtonsKonamiCodeEntry(this);
}

TEST_F(KonamiMetaGameE2ETestSuite, HardModeWinBoonAwarded) {
    konamiMetagameHardModeWinBoonAwarded(this);
}

TEST_F(KonamiMetaGameE2ETestSuite, MasteryReplayModeSelect) {
    konamiMetagameMasteryReplayModeSelect(this);
}

#endif // NATIVE_BUILD
