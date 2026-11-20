//
// CLI Test Entry Point
// Run with: pio test -e native_cli
//

#include <gtest/gtest.h>
#include <gmock/gmock.h>

// CLI-specific test headers
#include "cli-broker-tests.hpp"
#include "native-driver-tests.hpp"

// ============================================
// SERIAL CABLE BROKER TESTS
// ============================================

TEST_F(SerialCableBrokerTestSuite, ConnectHunterToBounty) {
    serialBrokerConnectHunterToBounty(this);
}

TEST_F(SerialCableBrokerTestSuite, DisconnectClearsConnection) {
    serialBrokerDisconnectClearsConnection(this);
}

TEST_F(SerialCableBrokerTestSuite, DoubleConnectionIdempotent) {
    serialBrokerDoubleConnectionIdempotent(this);
}

TEST_F(SerialCableBrokerTestSuite, InvalidDeviceReturnsFalse) {
    serialBrokerInvalidDeviceReturnsFalse(this);
}

// ============================================
// NATIVE PEER BROKER TESTS
// ============================================

TEST_F(NativePeerBrokerTestSuite, UnicastDelivery) {
    peerBrokerUnicastDelivery(this);
}

TEST_F(NativePeerBrokerTestSuite, BroadcastExcludesSender) {
    peerBrokerBroadcastExcludesSender(this);
}

TEST_F(NativePeerBrokerTestSuite, PeerRegistration) {
    peerBrokerPeerRegistration(this);
}

TEST_F(NativePeerBrokerTestSuite, UniqueMacGeneration) {
    peerBrokerUniqueMacGeneration(this);
}

TEST_F(NativePeerBrokerTestSuite, DisabledPeerNoReceive) { peerBrokerDisabledPeerNoReceive(this); }

// ============================================
// NATIVE SERIAL DRIVER TESTS
// ============================================

TEST_F(NativeSerialDriverTestSuite, OutputBufferMaxSize) {
    serialDriverOutputBufferMaxSize(this);
}

TEST_F(NativeSerialDriverTestSuite, InputQueueFIFO) {
    serialDriverInputQueueFIFO(this);
}

TEST_F(NativeSerialDriverTestSuite, AvailableForWrite) {
    serialDriverAvailableForWrite(this);
}

TEST_F(NativeSerialDriverTestSuite, TracksHistory) {
    serialDriverTracksHistory(this);
}

TEST_F(NativeSerialDriverTestSuite, ClearOutput) {
    serialDriverClearOutput(this);
}

TEST_F(NativeSerialDriverTestSuite, CallbackInvoked) {
    serialDriverCallbackInvoked(this);
}

TEST_F(NativeSerialDriverTestSuite, StripsFraming) {
    serialDriverStripsFraming(this);
}

// ============================================
// NATIVE PEER COMMS DRIVER TESTS
// ============================================

TEST_F(NativePeerCommsDriverTestSuite, TracksHistory) {
    peerCommsTracksHistory(this);
}

TEST_F(NativePeerCommsDriverTestSuite, StateString) {
    peerCommsStateString(this);
}

TEST_F(NativePeerCommsDriverTestSuite, MacString) {
    peerCommsMacString(this);
}

TEST_F(NativePeerCommsDriverTestSuite, CannotSendWhenDisabled) {
    peerCommsCannotSendWhenDisabled(this);
}

TEST_F(NativePeerCommsDriverTestSuite, HandlerRegistration) {
    peerCommsHandlerRegistration(this);
}

// ============================================
// NATIVE BUTTON DRIVER TESTS
// ============================================

TEST_F(NativeButtonDriverTestSuite, CallbackExecution) {
    buttonDriverCallbackExecution(this);
}

TEST_F(NativeButtonDriverTestSuite, ParameterizedCallback) {
    buttonDriverParameterizedCallback(this);
}

TEST_F(NativeButtonDriverTestSuite, RemoveCallbacks) {
    buttonDriverRemoveCallbacks(this);
}

// ============================================
// NATIVE LIGHT STRIP DRIVER TESTS
// ============================================

TEST_F(NativeLightStripDriverTestSuite, SetAndGet) {
    lightStripSetAndGet(this);
}

TEST_F(NativeLightStripDriverTestSuite, Clear) {
    lightStripClear(this);
}

TEST_F(NativeLightStripDriverTestSuite, BrightnessFloor) {
    lightStripBrightnessFloor(this);
}

TEST_F(NativeLightStripDriverTestSuite, GlobalBrightness) {
    lightStripGlobalBrightness(this);
}

// ============================================
// NATIVE DISPLAY DRIVER TESTS - Old Behavior
// ============================================

TEST_F(NativeDisplayDriverTestSuite, DrawTextAddsToHistory) {
    displayDriverDrawTextAddsToHistory(this);
}

TEST_F(NativeDisplayDriverTestSuite, DrawTextPositionedAddsToHistory) {
    displayDriverDrawTextPositionedAddsToHistory(this);
}

TEST_F(NativeDisplayDriverTestSuite, DrawButtonAddsToHistory) {
    displayDriverDrawButtonAddsToHistory(this);
}

TEST_F(NativeDisplayDriverTestSuite, InvalidateScreenClearsBuffer) {
    displayDriverInvalidateScreenClearsBuffer(this);
}

TEST_F(NativeDisplayDriverTestSuite, TextHistoryMaxSize) {
    displayDriverTextHistoryMaxSize(this);
}

TEST_F(NativeDisplayDriverTestSuite, TextHistoryNoDuplicates) {
    displayDriverTextHistoryNoDuplicates(this);
}

TEST_F(NativeDisplayDriverTestSuite, DrawTextRendersToBuffer) {
    displayDriverDrawTextRendersToBuffer(this);
}

TEST_F(NativeDisplayDriverTestSuite, MethodChainingWorks) {
    displayDriverMethodChainingWorks(this);
}

TEST_F(NativeDisplayDriverTestSuite, FontModeTracking) {
    displayDriverFontModeTracking(this);
}

// ============================================
// NATIVE DISPLAY DRIVER TESTS - XBM & Mirror
// ============================================

TEST_F(NativeDisplayDriverTestSuite, XBMDecoding) {
    displayDriverXBMDecoding(this);
}

TEST_F(NativeDisplayDriverTestSuite, FullWhiteImage) {
    displayDriverFullWhiteImage(this);
}

TEST_F(NativeDisplayDriverTestSuite, ImageOffset) {
    displayDriverImageOffset(this);
}

TEST_F(NativeDisplayDriverTestSuite, ImageSentinelPosition) {
    displayDriverImageSentinelPosition(this);
}

TEST_F(NativeDisplayDriverTestSuite, ImageExplicitPosition) {
    displayDriverImageExplicitPosition(this);
}

TEST_F(NativeDisplayDriverTestSuite, NullImageSafety) {
    displayDriverNullImageSafety(this);
}

TEST_F(NativeDisplayDriverTestSuite, DoubleDrawImage) {
    displayDriverDoubleDrawImage(this);
}

TEST_F(NativeDisplayDriverTestSuite, DrawImageThenText) {
    displayDriverDrawImageThenText(this);
}

TEST_F(NativeDisplayDriverTestSuite, RenderToAsciiDimensions) {
    displayDriverRenderToAsciiDimensions(this);
}

TEST_F(NativeDisplayDriverTestSuite, RenderToAsciiContent) {
    displayDriverRenderToAsciiContent(this);
}

TEST_F(NativeDisplayDriverTestSuite, RenderToAsciiFullBlock) {
    displayDriverRenderToAsciiFullBlock(this);
}

// ============================================
// NATIVE DISPLAY DRIVER TESTS - Braille
// ============================================

TEST_F(NativeDisplayDriverTestSuite, RenderToBrailleDimensions) {
    displayDriverRenderToBrailleDimensions(this);
}

TEST_F(NativeDisplayDriverTestSuite, RenderToBrailleBlank) {
    displayDriverRenderToBrailleBlank(this);
}

TEST_F(NativeDisplayDriverTestSuite, RenderToBrailleContent) {
    displayDriverRenderToBrailleContent(this);
}

TEST_F(NativeDisplayDriverTestSuite, RenderToBrailleFullBlock) {
    displayDriverRenderToBrailleFullBlock(this);
}

TEST_F(NativeDisplayDriverTestSuite, RenderToBrailleDotMapping) {
    displayDriverRenderToBrailleDotMapping(this);
}

// ============================================
// CLI DISPLAY COMMAND TESTS
// ============================================

TEST_F(CliDisplayCommandTestSuite, MirrorToggle) {
    cliCommandMirrorToggle(this);
}

TEST_F(CliDisplayCommandTestSuite, MirrorOnOff) {
    cliCommandMirrorOnOff(this);
}

TEST_F(CliDisplayCommandTestSuite, CaptionsToggle) {
    cliCommandCaptionsToggle(this);
}

TEST_F(CliDisplayCommandTestSuite, CaptionsOnOff) {
    cliCommandCaptionsOnOff(this);
}

TEST_F(CliDisplayCommandTestSuite, CaptionsAlias) {
    cliCommandCaptionsAlias(this);
}

TEST_F(CliDisplayCommandTestSuite, DisplayOn) {
    cliCommandDisplayOn(this);
}

TEST_F(CliDisplayCommandTestSuite, DisplayOff) {
    cliCommandDisplayOff(this);
}

TEST_F(CliDisplayCommandTestSuite, DisplayToggleBothOff) {
    cliCommandDisplayToggleBothOff(this);
}

TEST_F(CliDisplayCommandTestSuite, DisplayToggleBothOn) {
    cliCommandDisplayToggleBothOn(this);
}

TEST_F(CliDisplayCommandTestSuite, DisplayToggleDisagree) {
    cliCommandDisplayToggleDisagree(this);
}

TEST_F(CliDisplayCommandTestSuite, DisplayToggleDisagreeReverse) {
    cliCommandDisplayToggleDisagreeReverse(this);
}

TEST_F(CliDisplayCommandTestSuite, DisplayAlias) {
    cliCommandDisplayAlias(this);
}

// ============================================
// CLI ROLE COMMAND TESTS
// ============================================

TEST_F(CliRoleCommandTestSuite, ShowsSelectedDevice) {
    cliRoleCommandShowsSelectedDevice(this);
}

TEST_F(CliRoleCommandTestSuite, ShowsSpecificDevice) {
    cliRoleCommandShowsSpecificDevice(this);
}

TEST_F(CliRoleCommandTestSuite, ShowsAllDevices) {
    cliRoleCommandShowsAllDevices(this);
}

TEST_F(CliRoleCommandTestSuite, AliasWorks) {
    cliRoleCommandAliasWorks(this);
}

TEST_F(CliRoleCommandTestSuite, InvalidDevice) {
    cliRoleCommandInvalidDevice(this);
}

TEST_F(CliRoleCommandTestSuite, NoDevices) {
    cliRoleCommandNoDevices(this);
}

// ============================================
// CLI COMMAND / REBOOT TESTS
// ============================================

TEST(CliCommandTest, RebootEntersPinEntry) { cliCommandRebootEntersPinEntry(); }

TEST(CliCommandTest, RebootedDeviceReRegistersWithTestIdentity) {
    cliRebootedDeviceReRegistersWithTestIdentity();
}

TEST_F(CliSimulationTestSuite, DevicesStartWithDistinctTestIdentities) {
    cliDevicesStartWithDistinctTestIdentities(this);
}

TEST_F(CliSimulationTestSuite, CabledDevicesConnect) { cliSimulationCabledDevicesConnect(this); }

TEST_F(CliSimulationTestSuite, DuelDrawnByTypedButtonsStoresOneRecordOnEachPdn) {
    cliDuelDrawnByTypedButtonsStoresOneRecordOnEachPdn(this);
}

TEST_F(CliSimulationTestSuite, FdnBeaconCollectsBothDuelRecords) {
    cliFdnBeaconCollectsBothDuelRecords(this);
}

// ============================================
// MAIN
// ============================================

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
