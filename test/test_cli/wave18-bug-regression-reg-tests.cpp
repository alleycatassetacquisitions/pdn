#ifdef NATIVE_BUILD

#include "wave18-bug-regression-tests.hpp"

// ============================================
// WAVE 18 BUG REGRESSION TESTS - Registration
// ============================================

// Bug #204: konami CLI command resets progress to zero
TEST_F(Wave18BugRegressionTestSuite, KonamiCLIDoesNotResetProgress) {
    testKonamiCLIDoesNotResetProgress(this);
}

// Bug #206: FdnDetected receives empty message on game resume
TEST_F(Wave18BugRegressionTestSuite, DISABLED_FdnDetectedMessageNotEmptyOnResume) {
    testFdnDetectedMessageNotEmptyOnResume(this);
}

// Bug #230: Player LEDs show (0,0,0) during FdnDetected
TEST_F(Wave18BugRegressionTestSuite, FdnDetectedLEDsNotBlack) {
    testFdnDetectedLEDsNotBlack(this);
}

#endif // NATIVE_BUILD
