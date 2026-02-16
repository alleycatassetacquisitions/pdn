//
// FDN Protocol Tests — Protocol parsing, FDN Game, CLI FDN Device, Device Extensions
//

#include <gtest/gtest.h>

#include "fdn-protocol-tests.hpp"
#include "fdn-game-tests.hpp"
#include "cli-fdn-tests.hpp"
#include "device-extension-tests.hpp"

// ============================================
// FDN PROTOCOL TESTS
// ============================================

TEST_F(FdnProtocolTestSuite, ParseValidMessage) {
    fdnProtocolParseValidMessage(this);
}

TEST_F(FdnProtocolTestSuite, ParseGhostRunner) {
    fdnProtocolParseGhostRunner(this);
}

TEST_F(FdnProtocolTestSuite, RejectInvalidPrefix) {
    fdnProtocolRejectInvalidPrefix(this);
}

TEST_F(FdnProtocolTestSuite, RejectShortMessage) {
    fdnProtocolRejectShortMessage(this);
}

TEST_F(FdnProtocolTestSuite, RejectOutOfRange) {
    fdnProtocolRejectOutOfRange(this);
}

TEST_F(FdnProtocolTestSuite, ParseGresMessage) {
    fdnProtocolParseGresMessage(this);
}

TEST_F(FdnProtocolTestSuite, ParseGresInvalid) {
    fdnProtocolParseGresInvalid(this);
}

// ============================================
// FDN GAME TESTS
// ============================================

TEST_F(FdnGameTestSuite, DeviceType) {
    fdnGameDeviceType(this);
}

TEST_F(FdnGameTestSuite, StartsInNpcIdle) {
    fdnGameStartsInNpcIdle(this);
}

TEST_F(FdnGameTestSuite, NpcIdleBroadcasts) {
    fdnGameNpcIdleBroadcasts(this);
}

TEST_F(FdnGameTestSuite, DISABLED_NpcIdleTransitionsOnMac) {
    fdnGameNpcIdleTransitionsOnMac(this);
}

TEST_F(FdnGameTestSuite, HandshakeSendsFack) {
    fdnGameHandshakeSendsFack(this);
}

TEST_F(FdnGameTestSuite, DISABLED_HandshakeTimeout) {
    fdnGameHandshakeTimeout(this);
}

TEST_F(FdnGameTestSuite, DISABLED_TracksTypeAndReward) {
    fdnGameTracksTypeAndReward(this);
}

TEST_F(FdnGameTestSuite, LastResultTracking) {
    fdnGameLastResultTracking(this);
}

// ============================================
// CLI FDN DEVICE TESTS
// ============================================

TEST_F(CliFdnTestSuite, CreateDeviceType) {
    cliFdnCreateDeviceType(this);
}

TEST_F(CliFdnTestSuite, DeviceIdRange) {
    cliFdnDeviceIdRange(this);
}

TEST_F(CliFdnTestSuite, RegistersWithBroker) {
    cliFdnRegistersWithBroker(this);
}

TEST_F(CliFdnTestSuite, IdleDisplaysColorProfile) {
    cliFdnIdleDisplaysColorProfile(this);
}

// ============================================
// DEVICE EXTENSION TESTS
// ============================================

TEST_F(DeviceExtensionTestSuite, GetAppReturnsApp) {
    deviceExtensionGetAppReturnsApp(this);
}

TEST_F(DeviceExtensionTestSuite, GetAppReturnsNull) {
    deviceExtensionGetAppReturnsNull(this);
}

// DISABLED: SIGSEGV — returnToPreviousApp() crashes during app resume.
// Root cause: 3-level app stack (Quickdraw→KMG→minigame) + single previousAppId.
// See issue #327 for full analysis.
TEST_F(DeviceExtensionTestSuite, DISABLED_ReturnToPrevious) {
    deviceExtensionReturnToPrevious(this);
}
