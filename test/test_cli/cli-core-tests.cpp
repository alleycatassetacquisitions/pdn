//
// Core CLI Tests — Broker, HTTP, Serial, Peer, Button, Light, Display, Command, Role, Reboot
//

#include <gtest/gtest.h>

#include "cli-broker-tests.hpp"
#include "cli-http-server-tests.hpp"
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

TEST_F(NativePeerBrokerTestSuite, DisconnectedPeerNoReceive) {
    peerBrokerDisconnectedPeerNoReceive(this);
}

// ============================================
// MOCK HTTP SERVER TESTS
// ============================================

TEST_F(MockHttpServerTestSuite, GetPlayerReturnsCorrectData) {
    httpServerGetPlayerReturnsCorrectData(this);
}

TEST_F(MockHttpServerTestSuite, GetPlayerReturnsDefaults) {
    httpServerGetPlayerReturnsDefaults(this);
}

TEST_F(MockHttpServerTestSuite, PutMatchesAccepts) {
    httpServerPutMatchesAccepts(this);
}

TEST_F(MockHttpServerTestSuite, UnknownEndpointReturns404) {
    httpServerUnknownEndpointReturns404(this);
}

TEST_F(MockHttpServerTestSuite, TracksHistory) {
    httpServerTracksHistory(this);
}

TEST_F(MockHttpServerTestSuite, OfflineModeSimulation) {
    httpServerOfflineModeSimulation(this);
}

TEST_F(MockHttpServerTestSuite, ClearHistory) {
    httpServerClearHistory(this);
}

// ============================================
// NATIVE HTTP CLIENT DRIVER TESTS
// ============================================

TEST_F(NativeHttpClientDriverTestSuite, ProcessesRequests) {
    httpClientProcessesRequests(this);
}

TEST_F(NativeHttpClientDriverTestSuite, HandlesOfflineServer) {
    httpClientHandlesOfflineServer(this);
}

TEST_F(NativeHttpClientDriverTestSuite, TracksHistory) {
    httpClientTracksHistory(this);
}

TEST_F(NativeHttpClientDriverTestSuite, DisabledMockServerFails) {
    httpClientDisabledMockServerFails(this);
}

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

TEST_F(NativePeerCommsDriverTestSuite, CannotSendWhenDisconnected) {
    peerCommsCannotSendWhenDisconnected(this);
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

TEST_F(CliCommandTestSuite, MockHttpFetchTransitions) {
    cliDeviceMockHttpFetchTransitions(this);
}

TEST_F(CliCommandTestSuite, RebootResetsState) {
    cliCommandRebootResetsState(this);
}

TEST_F(CliCommandTestSuite, RebootFromLaterState) {
    cliCommandRebootFromLaterState(this);
}

TEST_F(CliCommandTestSuite, RebootClearsHistory) {
    cliCommandRebootClearsHistory(this);
}

// ============================================
// CLI COMMAND PROCESSOR EDGE CASE TESTS
// ============================================

#include "cli-command-edge-tests.hpp"

// Help command tests
TEST_F(CliCommandProcessorTestSuite, HelpCommandNoArgs) {
    helpCommandNoArgs(this);
}

TEST_F(CliCommandProcessorTestSuite, HelpCommandAliases) {
    helpCommandAliases(this);
}

TEST_F(CliCommandProcessorTestSuite, Help2CommandShowsExtendedHelp) {
    help2CommandShowsExtendedHelp(this);
}

// Quit command tests
TEST_F(CliCommandProcessorTestSuite, QuitCommandSetsShouldQuit) {
    quitCommandSetsShouldQuit(this);
}

TEST_F(CliCommandProcessorTestSuite, QuitCommandAliases) {
    quitCommandAliases(this);
}

// Cable/connect command tests
TEST_F(CliCommandProcessorTestSuite, CableCommandNoArgs) {
    cableCommandNoArgs(this);
}

TEST_F(CliCommandProcessorTestSuite, CableCommandInvalidDeviceId) {
    cableCommandInvalidDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, CableCommandNegativeDeviceId) {
    cableCommandNegativeDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, CableCommandConnectToSelf) {
    cableCommandConnectToSelf(this);
}

TEST_F(CliCommandProcessorTestSuite, CableCommandNonNumericId) {
    cableCommandNonNumericId(this);
}

TEST_F(CliCommandProcessorTestSuite, CableCommandAliases) {
    cableCommandAliases(this);
}

TEST_F(CliCommandProcessorTestSuite, CableDisconnectNoArgs) {
    cableDisconnectNoArgs(this);
}

TEST_F(CliCommandProcessorTestSuite, CableListNoConnections) {
    cableListNoConnections(this);
}

// Reboot command tests
TEST_F(CliCommandProcessorTestSuite, RebootCommandNoArgs) {
    rebootCommandNoArgs(this);
}

TEST_F(CliCommandProcessorTestSuite, RebootCommandInvalidDeviceId) {
    rebootCommandInvalidDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, RebootCommandExtraArgs) {
    rebootCommandExtraArgs(this);
}

TEST_F(CliCommandProcessorTestSuite, RebootCommandAlias) {
    rebootCommandAlias(this);
}

// Display command tests
TEST_F(CliCommandProcessorTestSuite, DisplayCommandToggle) {
    displayCommandToggle(this);
}

TEST_F(CliCommandProcessorTestSuite, DisplayCommandOn) {
    displayCommandOn(this);
}

TEST_F(CliCommandProcessorTestSuite, DisplayCommandOff) {
    displayCommandOff(this);
}

TEST_F(CliCommandProcessorTestSuite, DisplayCommandInvalidArg) {
    displayCommandInvalidArg(this);
}

TEST_F(CliCommandProcessorTestSuite, DisplayCommandAlias) {
    displayCommandAlias(this);
}

// Role command tests
TEST_F(CliCommandProcessorTestSuite, RoleCommandShowsSelectedDevice) {
    roleCommandShowsSelectedDevice(this);
}

TEST_F(CliCommandProcessorTestSuite, RoleCommandWithDeviceId) {
    roleCommandWithDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, RoleCommandAll) {
    roleCommandAll(this);
}

TEST_F(CliCommandProcessorTestSuite, RoleCommandInvalidDeviceId) {
    roleCommandInvalidDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, RoleCommandAlias) {
    roleCommandAlias(this);
}

// Edge case tests
TEST_F(CliCommandProcessorTestSuite, EmptyInputNoOp) {
    emptyInputNoOp(this);
}

TEST_F(CliCommandProcessorTestSuite, WhitespaceOnlyInput) {
    whitespaceOnlyInput(this);
}

TEST_F(CliCommandProcessorTestSuite, UnknownCommandShowsError) {
    unknownCommandShowsError(this);
}

TEST_F(CliCommandProcessorTestSuite, CommandWithLeadingWhitespace) {
    commandWithLeadingWhitespace(this);
}

TEST_F(CliCommandProcessorTestSuite, CommandWithTrailingWhitespace) {
    commandWithTrailingWhitespace(this);
}

TEST_F(CliCommandProcessorTestSuite, CommandWithMultipleSpaces) {
    commandWithMultipleSpaces(this);
}

TEST_F(CliCommandProcessorTestSuite, VeryLongInputString) {
    veryLongInputString(this);
}

TEST_F(CliCommandProcessorTestSuite, CommandWithSpecialCharacters) {
    commandWithSpecialCharacters(this);
}

// List and select command tests
TEST_F(CliCommandProcessorTestSuite, ListCommandShowsAllDevices) {
    listCommandShowsAllDevices(this);
}

TEST_F(CliCommandProcessorTestSuite, ListCommandAlias) {
    listCommandAlias(this);
}

TEST_F(CliCommandProcessorTestSuite, SelectCommandNoArgs) {
    selectCommandNoArgs(this);
}

TEST_F(CliCommandProcessorTestSuite, SelectCommandValidDeviceId) {
    selectCommandValidDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, SelectCommandInvalidDeviceId) {
    selectCommandInvalidDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, SelectCommandAlias) {
    selectCommandAlias(this);
}

// State command tests
TEST_F(CliCommandProcessorTestSuite, StateCommandShowsCurrentState) {
    stateCommandShowsCurrentState(this);
}

TEST_F(CliCommandProcessorTestSuite, StateCommandWithDeviceId) {
    stateCommandWithDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, StateCommandInvalidDeviceId) {
    stateCommandInvalidDeviceId(this);
}

TEST_F(CliCommandProcessorTestSuite, StateCommandAlias) {
    stateCommandAlias(this);
}

// Games command tests
TEST_F(CliCommandProcessorTestSuite, GamesCommandListsAllGames) {
    gamesCommandListsAllGames(this);
}

// Mirror and captions command tests
TEST_F(CliCommandProcessorTestSuite, MirrorCommandToggle) {
    mirrorCommandToggle(this);
}

TEST_F(CliCommandProcessorTestSuite, MirrorCommandOn) {
    mirrorCommandOn(this);
}

TEST_F(CliCommandProcessorTestSuite, MirrorCommandOff) {
    mirrorCommandOff(this);
}

TEST_F(CliCommandProcessorTestSuite, MirrorCommandInvalidArg) {
    mirrorCommandInvalidArg(this);
}

TEST_F(CliCommandProcessorTestSuite, MirrorCommandAlias) {
    mirrorCommandAlias(this);
}

TEST_F(CliCommandProcessorTestSuite, CaptionsCommandToggle) {
    captionsCommandToggle(this);
}

TEST_F(CliCommandProcessorTestSuite, CaptionsCommandOn) {
    captionsCommandOn(this);
}

TEST_F(CliCommandProcessorTestSuite, CaptionsCommandOff) {
    captionsCommandOff(this);
}

TEST_F(CliCommandProcessorTestSuite, CaptionsCommandInvalidArg) {
    captionsCommandInvalidArg(this);
}

TEST_F(CliCommandProcessorTestSuite, CaptionsCommandAlias) {
    captionsCommandAlias(this);
}
