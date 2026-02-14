#ifdef NATIVE_BUILD

#include "konami-fdn-display-tests.hpp"

// ============================================
// KONAMI FDN DISPLAY TEST REGISTRATIONS
// ============================================

TEST_F(KonamiFdnDisplayTestSuite, MountsIdle) {
    konamiFdnDisplayMountsIdle(this);
}

TEST_F(KonamiFdnDisplayTestSuite, RevealSignalEcho) {
    konamiFdnDisplayRevealSignalEcho(this);
}

TEST_F(KonamiFdnDisplayTestSuite, RevealGhostRunner) {
    konamiFdnDisplayRevealGhostRunner(this);
}

TEST_F(KonamiFdnDisplayTestSuite, RevealSpikeVector) {
    konamiFdnDisplayRevealSpikeVector(this);
}

TEST_F(KonamiFdnDisplayTestSuite, RevealFirewallDecrypt) {
    konamiFdnDisplayRevealFirewallDecrypt(this);
}

TEST_F(KonamiFdnDisplayTestSuite, RevealCipherPath) {
    konamiFdnDisplayRevealCipherPath(this);
}

TEST_F(KonamiFdnDisplayTestSuite, RevealExploitSequencer) {
    konamiFdnDisplayRevealExploitSequencer(this);
}

TEST_F(KonamiFdnDisplayTestSuite, RevealBreachDefense) {
    konamiFdnDisplayRevealBreachDefense(this);
}

TEST_F(KonamiFdnDisplayTestSuite, FullReveal) {
    konamiFdnDisplayFullReveal(this);
}

TEST_F(KonamiFdnDisplayTestSuite, MappingCorrect) {
    konamiFdnDisplayMappingCorrect(this);
}

TEST_F(KonamiFdnDisplayTestSuite, SymbolCycling) {
    konamiFdnDisplaySymbolCycling(this);
}

TEST_F(KonamiFdnDisplayTestSuite, CorrectSymbols) {
    konamiFdnDisplayCorrectSymbols(this);
}

#endif // NATIVE_BUILD
