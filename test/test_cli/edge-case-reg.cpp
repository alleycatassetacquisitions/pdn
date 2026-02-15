//
// Edge Case Tests — Registration file for boundary and edge case testing
//

#include <gtest/gtest.h>

#include "edge-case-tests.hpp"

// ============================================
// BOUNDARY VALUE TESTS
// ============================================

TEST_F(EdgeCaseTestSuite, KonamiProgressZero) {
    edgeCaseKonamiProgressZero(this);
}

TEST_F(EdgeCaseTestSuite, KonamiProgressOne) {
    edgeCaseKonamiProgressOne(this);
}

TEST_F(EdgeCaseTestSuite, KonamiProgressSixButtons) {
    edgeCaseKonamiProgressSixButtons(this);
}

TEST_F(EdgeCaseTestSuite, KonamiProgressMax) {
    edgeCaseKonamiProgressMax(this);
}

TEST_F(EdgeCaseTestSuite, AttemptCountZero) {
    edgeCaseAttemptCountZero(this);
}

TEST_F(EdgeCaseTestSuite, AttemptCountOne) {
    edgeCaseAttemptCountOne(this);
}

TEST_F(EdgeCaseTestSuite, AttemptCountMax) {
    edgeCaseAttemptCountMax(this);
}

TEST_F(EdgeCaseTestSuite, ScoreZero) {
    edgeCaseScoreZero(this);
}

TEST_F(EdgeCaseTestSuite, ScoreOne) {
    edgeCaseScoreOne(this);
}

TEST_F(EdgeCaseTestSuite, ScoreMax) {
    edgeCaseScoreMax(this);
}

// ============================================
// KONAMI SYSTEM EDGE CASES
// ============================================

TEST_F(EdgeCaseTestSuite, KonamiReverseOrder) {
    edgeCaseKonamiReverseOrder(this);
}

TEST_F(EdgeCaseTestSuite, KonamiRandomOrder) {
    edgeCaseKonamiRandomOrder(this);
}

TEST_F(EdgeCaseTestSuite, KonamiDuplicateUnlock) {
    edgeCaseKonamiDuplicateUnlock(this);
}

TEST_F(EdgeCaseTestSuite, ColorProfileNotEligible) {
    edgeCaseColorProfileNotEligible(this);
}

TEST_F(EdgeCaseTestSuite, ColorProfileCycle) {
    edgeCaseColorProfileCycle(this);
}

// ============================================
// PERSISTENCE EDGE CASES
// ============================================

TEST_F(EdgeCaseTestSuite, PersistenceMaxValues) {
    edgeCasePersistenceMaxValues(this);
}

TEST_F(EdgeCaseTestSuite, PersistenceMissingKeys) {
    edgeCasePersistenceMissingKeys(this);
}

TEST_F(EdgeCaseTestSuite, PersistenceConcurrentSaves) {
    edgeCasePersistenceConcurrentSaves(this);
}

// ============================================
// NPC/FDN EDGE CASES
// ============================================

TEST_F(FdnEdgeCaseTestSuite, NpcIdleTimeout) {
    edgeCaseNpcIdleTimeout(this);
}

TEST_F(FdnEdgeCaseTestSuite, NpcHandshakeTimeout) {
    edgeCaseNpcHandshakeTimeout(this);
}

TEST_F(FdnEdgeCaseTestSuite, NpcMalformedData) {
    edgeCaseNpcMalformedData(this);
}

TEST_F(FdnEdgeCaseTestSuite, NpcDisconnectMidGame) {
    edgeCaseNpcDisconnectMidGame(this);
}

TEST_F(EdgeCaseTestSuite, FdnResultManagerMaxCapacity) {
    edgeCaseFdnResultManagerMaxCapacity(this);
}

TEST_F(EdgeCaseTestSuite, FdnResultManagerClearEmpty) {
    edgeCaseFdnResultManagerClearEmpty(this);
}

TEST_F(FdnEdgeCaseTestSuite, NpcBufferFloodOnConnect) {
    edgeCaseNpcBufferFloodOnConnect(this);
}

TEST_F(FdnEdgeCaseTestSuite, NpcBroadcastAfterConnect) {
    edgeCaseNpcBroadcastAfterConnect(this);
}

TEST_F(FdnEdgeCaseTestSuite, NpcReconnectClearsBuffer) {
    edgeCaseNpcReconnectClearsBuffer(this);
}

// ============================================
// DEVICE LIFECYCLE EDGE CASES
// ============================================

TEST_F(EdgeCaseTestSuite, DeviceDestructorDismountsActiveApp) {
    edgeCaseDeviceDestructorDismountsActiveApp(this);
}
