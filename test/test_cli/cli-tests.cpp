//
// CLI Test Entry Point
// Run with: pio test -e native_cli
//

#include <gtest/gtest.h>
#include <gmock/gmock.h>

// CLI-specific test headers
#include "cli-broker-tests.hpp"
#include "cli-http-server-tests.hpp"
#include "native-driver-tests.hpp"
#include "fdn-protocol-tests.hpp"
#include "fdn-game-tests.hpp"
#include "cli-fdn-tests.hpp"
#include "device-extension-tests.hpp"
#include "signal-echo-tests.hpp"
#include "fdn-integration-tests.hpp"
#include "progression-core-tests.hpp"
#include "color-profile-tests.hpp"
#include "firewall-decrypt-tests.hpp"
#include "progression-e2e-tests.hpp"
#include "negative-flow-tests.hpp"
#include "ghost-runner-tests.hpp"
#include "spike-vector-tests.hpp"
#include "cipher-path-tests.hpp"
#include "exploit-sequencer-tests.hpp"
#include "breach-defense-tests.hpp"

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

TEST_F(FdnGameTestSuite, NpcIdleTransitionsOnMac) {
    fdnGameNpcIdleTransitionsOnMac(this);
}

TEST_F(FdnGameTestSuite, HandshakeSendsFack) {
    fdnGameHandshakeSendsFack(this);
}

TEST_F(FdnGameTestSuite, HandshakeTimeout) {
    fdnGameHandshakeTimeout(this);
}

TEST_F(FdnGameTestSuite, TracksTypeAndReward) {
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

// ============================================
// DEVICE EXTENSION TESTS
// ============================================

TEST_F(DeviceExtensionTestSuite, GetAppReturnsApp) {
    deviceExtensionGetAppReturnsApp(this);
}

TEST_F(DeviceExtensionTestSuite, GetAppReturnsNull) {
    deviceExtensionGetAppReturnsNull(this);
}

TEST_F(DeviceExtensionTestSuite, ReturnToPrevious) {
    deviceExtensionReturnToPrevious(this);
}

// ============================================
// SIGNAL ECHO TESTS
// ============================================

TEST_F(SignalEchoTestSuite, SequenceGenerationLength) {
    echoSequenceGenerationLength(this);
}

TEST_F(SignalEchoTestSuite, SequenceGenerationMixed) {
    echoSequenceGenerationMixed(this);
}

TEST_F(SignalEchoTestSuite, IntroTransitionsToShow) {
    echoIntroTransitionsToShow(this);
}

TEST_F(SignalEchoTestSuite, ShowSequenceDisplaysSignals) {
    echoShowSequenceDisplaysSignals(this);
}

TEST_F(SignalEchoTestSuite, ShowTransitionsToInput) {
    echoShowTransitionsToInput(this);
}

TEST_F(SignalEchoTestSuite, CorrectInputAdvancesIndex) {
    echoCorrectInputAdvancesIndex(this);
}

TEST_F(SignalEchoTestSuite, WrongInputCountsMistake) {
    echoWrongInputCountsMistake(this);
}

TEST_F(SignalEchoTestSuite, AllCorrectInputsNextRound) {
    echoAllCorrectInputsNextRound(this);
}

TEST_F(SignalEchoTestSuite, MistakesExhaustedLose) {
    echoMistakesExhaustedLose(this);
}

TEST_F(SignalEchoTestSuite, AllRoundsCompletedWin) {
    echoAllRoundsCompletedWin(this);
}

TEST_F(SignalEchoTestSuite, CumulativeModeAppends) {
    echoCumulativeModeAppends(this);
}

TEST_F(SignalEchoTestSuite, FreshModeNewSequence) {
    echoFreshModeNewSequence(this);
}

TEST_F(SignalEchoTestSuite, WinSetsOutcome) {
    echoWinSetsOutcome(this);
}

TEST_F(SignalEchoTestSuite, LoseSetsOutcome) {
    echoLoseSetsOutcome(this);
}

TEST_F(SignalEchoTestSuite, IsGameCompleteAfterWin) {
    echoIsGameCompleteAfterWin(this);
}

TEST_F(SignalEchoTestSuite, ResetGameClearsOutcome) {
    echoResetGameClearsOutcome(this);
}

TEST_F(SignalEchoTestSuite, StandaloneRestartAfterWin) {
    echoStandaloneRestartAfterWin(this);
}

// ============================================
// SIGNAL ECHO DIFFICULTY TESTS
// ============================================

TEST_F(SignalEchoDifficultyTestSuite, EasySequenceLength) {
    echoDiffEasySequenceLength(this);
}

TEST_F(SignalEchoDifficultyTestSuite, Easy3MistakesAllowed) {
    echoDiffEasy3MistakesAllowed(this);
}

TEST_F(SignalEchoDifficultyTestSuite, HardSequenceLength) {
    echoDiffHardSequenceLength(this);
}

TEST_F(SignalEchoDifficultyTestSuite, Hard1MistakeAllowed) {
    echoDiffHard1MistakeAllowed(this);
}

TEST_F(SignalEchoDifficultyTestSuite, EasyWinOutcome) {
    echoDiffEasyWinOutcome(this);
}

TEST_F(SignalEchoDifficultyTestSuite, HardWinOutcome) {
    echoDiffHardWinOutcome(this);
}

TEST_F(SignalEchoDifficultyTestSuite, LifeIndicator) {
    echoDiffLifeIndicator(this);
}

TEST_F(SignalEchoDifficultyTestSuite, WrongInputAdvances) {
    echoDiffWrongInputAdvances(this);
}

// ============================================
// FDN INTEGRATION TESTS
// ============================================

TEST_F(FdnIntegrationTestSuite, DetectsFdnBroadcast) {
    fdnIntegrationDetectsFdnBroadcast(this);
}

TEST_F(FdnIntegrationTestSuite, HandshakeSendsMAC) {
    fdnIntegrationHandshakeSendsMAC(this);
}

TEST_F(FdnIntegrationTestSuite, TransitionsToSignalEcho) {
    fdnIntegrationTransitionsToSignalEcho(this);
}

TEST_F(FdnIntegrationTestSuite, HandshakeTimeout) {
    fdnIntegrationHandshakeTimeout(this);
}

// ============================================
// FDN COMPLETE TESTS
// ============================================

TEST_F(FdnCompleteTestSuite, ShowsVictoryOnWin) {
    fdnCompleteShowsVictoryOnWin(this);
}

TEST_F(FdnCompleteTestSuite, UnlocksKonamiOnWin) {
    fdnCompleteUnlocksKonamiOnWin(this);
}

TEST_F(FdnCompleteTestSuite, UnlocksColorProfileOnHardWin) {
    fdnCompleteUnlocksColorProfileOnHardWin(this);
}

TEST_F(FdnCompleteTestSuite, ShowsDefeatedOnLoss) {
    fdnCompleteShowsDefeatedOnLoss(this);
}

TEST_F(FdnCompleteTestSuite, TransitionsToIdleAfterTimer) {
    fdnCompleteTransitionsToIdleAfterTimer(this);
}

TEST_F(FdnCompleteTestSuite, ClearsPendingChallenge) {
    fdnCompleteClearsPendingChallenge(this);
}

// ============================================
// PROGRESS MANAGER TESTS
// ============================================

TEST_F(ProgressManagerTestSuite, SavesOnWin) {
    progressManagerSavesOnWin(this);
}

TEST_F(ProgressManagerTestSuite, LoadsProgress) {
    progressManagerLoadsProgress(this);
}

TEST_F(ProgressManagerTestSuite, ClearsProgress) {
    progressManagerClearsProgress(this);
}

// ============================================
// PLAYER CHALLENGE TESTS
// ============================================

TEST_F(PlayerChallengeTestSuite, KonamiUnlockAndQuery) {
    playerKonamiUnlockAndQuery(this);
}

TEST_F(PlayerChallengeTestSuite, PendingChallengeSetClear) {
    playerPendingChallengeSetClear(this);
}

TEST_F(PlayerChallengeTestSuite, ColorProfileEligibility) {
    playerColorProfileEligibility(this);
}

TEST_F(PlayerChallengeTestSuite, EquippedColorProfile) {
    playerEquippedColorProfile(this);
}

TEST_F(PlayerChallengeTestSuite, KonamiProgressSetGet) {
    playerKonamiProgressSetGet(this);
}

// ============================================
// APP SWITCHING TESTS
// ============================================

TEST_F(AppSwitchingTestSuite, SignalEchoRegistered) {
    appSwitchingSignalEchoRegistered(this);
}

TEST_F(AppSwitchingTestSuite, QuickdrawActiveAtStart) {
    appSwitchingQuickdrawActiveAtStart(this);
}

TEST_F(AppSwitchingTestSuite, ReturnToPrevious) {
    appSwitchingReturnToPrevious(this);
}

// ============================================
// DIFFICULTY GATING TESTS
// ============================================

TEST_F(DifficultyGatingTestSuite, EasyWithoutBoon) {
    difficultyGatingEasyWithoutBoon(this);
}

TEST_F(DifficultyGatingTestSuite, HardWithBoon) {
    difficultyGatingHardWithBoon(this);
}

TEST_F(DifficultyGatingTestSuite, SetsLastGameType) {
    difficultyGatingSetsLastGameType(this);
}

TEST_F(DifficultyGatingTestSuite, UnknownGameIdle) {
    difficultyGatingUnknownGameIdle(this);
}

// ============================================
// AUTO-BOON TESTS
// ============================================

TEST_F(AutoBoonTestSuite, TriggersOnSeventhButton) {
    autoBoonTriggersOnSeventhButton(this);
}

TEST_F(AutoBoonTestSuite, DoesNotRetrigger) {
    autoBoonDoesNotRetrigger(this);
}

TEST_F(AutoBoonTestSuite, ShowsDisplay) {
    autoBoonShowsDisplay(this);
}

TEST_F(AutoBoonTestSuite, HardWinSetsPending) {
    autoBoonHardWinSetsPending(this);
}

TEST_F(AutoBoonTestSuite, EasyWinNoPending) {
    autoBoonEasyWinNoPending(this);
}

// ============================================
// PLAYER FIELD ACCESSOR TESTS (extended)
// ============================================

TEST_F(PlayerChallengeTestSuite, IsKonamiComplete) {
    playerIsKonamiComplete(this);
}

TEST_F(PlayerChallengeTestSuite, KonamiBoonSetGet) {
    playerKonamiBoonSetGet(this);
}

TEST_F(PlayerChallengeTestSuite, LastFdnGameTypeSetGet) {
    playerLastFdnGameTypeSetGet(this);
}

TEST_F(PlayerChallengeTestSuite, PendingProfileGameSetGet) {
    playerPendingProfileGameSetGet(this);
}

TEST_F(PlayerChallengeTestSuite, ColorProfileEligibilitySet) {
    playerColorProfileEligibilitySet(this);
}

// ============================================
// PROGRESS MANAGER EXTENDED TESTS
// ============================================

TEST_F(ProgressManagerTestSuite, SaveLoadBoon) {
    progressManagerSaveLoadBoon(this);
}

TEST_F(ProgressManagerTestSuite, SaveLoadProfile) {
    progressManagerSaveLoadProfile(this);
}

TEST_F(ProgressManagerTestSuite, SaveLoadEligibility) {
    progressManagerSaveLoadEligibility(this);
}

TEST_F(ProgressManagerTestSuite, ClearAll) {
    progressManagerClearAll(this);
}

TEST_F(ProgressManagerTestSuite, SyncCallsServer) {
    progressManagerSyncCallsServer(this);
}

// ============================================
// GAME ROUTING TESTS
// ============================================

TEST_F(GameRoutingTestSuite, AppIdForGame) {
    gameRoutingSignalEcho(this);
}

// ============================================
// KONAMI COMMAND TESTS
// ============================================

TEST_F(KonamiCommandTestSuite, ShowsProgress) {
    konamiCommandShowsProgress(this);
}

TEST_F(KonamiCommandTestSuite, SetsProgress) {
    konamiCommandSetsProgress(this);
}

// ============================================
// COLOR PROFILE LOOKUP TESTS
// ============================================

TEST_F(ColorProfileLookupTestSuite, SignalEcho) {
    colorProfileLookupSignalEcho(this);
}

TEST_F(ColorProfileLookupTestSuite, FirewallDecrypt) {
    colorProfileLookupFirewallDecrypt(this);
}

TEST_F(ColorProfileLookupTestSuite, Default) {
    colorProfileLookupDefault(this);
}

TEST_F(ColorProfileLookupTestSuite, Names) {
    colorProfileLookupNames(this);
}

// ============================================
// COLOR PROFILE PROMPT TESTS
// ============================================

TEST_F(ColorProfilePromptTestSuite, YesEquips) {
    colorProfilePromptYesEquips(this);
}

TEST_F(ColorProfilePromptTestSuite, NoDoesNotEquip) {
    colorProfilePromptNoDoesNotEquip(this);
}

TEST_F(ColorProfilePromptTestSuite, AutoDismiss) {
    colorProfilePromptAutoDismiss(this);
}

TEST_F(ColorProfilePromptTestSuite, ShowsDisplay) {
    colorProfilePromptShowsDisplay(this);
}

TEST_F(ColorProfilePromptTestSuite, ClearsPending) {
    colorProfilePromptClearsPending(this);
}

// ============================================
// COLOR PROFILE PICKER TESTS
// ============================================

TEST_F(ColorProfilePickerTestSuite, ShowsHeader) {
    colorProfilePickerShowsHeader(this);
}

TEST_F(ColorProfilePickerTestSuite, ScrollWraps) {
    colorProfilePickerScrollWraps(this);
}

TEST_F(ColorProfilePickerTestSuite, SelectDefault) {
    colorProfilePickerSelectDefault(this);
}

TEST_F(ColorProfilePickerTestSuite, TransitionsToIdle) {
    colorProfilePickerTransitionsToIdle(this);
}

TEST_F(ColorProfilePickerTestSuite, PreselectsEquipped) {
    colorProfilePickerPreselectsEquipped(this);
}

// ============================================
// FDN COMPLETE ROUTING TESTS
// ============================================

TEST_F(FdnCompleteRoutingTestSuite, RoutesToPromptOnHardWin) {
    fdnCompleteRoutesToPromptOnHardWin(this);
}

TEST_F(FdnCompleteRoutingTestSuite, RoutesToIdleOnEasyWin) {
    fdnCompleteRoutesToIdleOnEasyWin(this);
}

TEST_F(FdnCompleteRoutingTestSuite, RoutesToIdleOnLoss) {
    fdnCompleteRoutesToIdleOnLoss(this);
}

// ============================================
// IDLE COLOR PICKER ENTRY TESTS
// ============================================

TEST_F(IdleColorPickerTestSuite, LongPressEntersPicker) {
    idleLongPressEntersPicker(this);
}

TEST_F(IdleColorPickerTestSuite, LongPressNoPickerWithoutEligibility) {
    idleLongPressNoPickerWithoutEligibility(this);
}

// ============================================
// STATE NAME TESTS
// ============================================

TEST_F(StateNameTestSuite, ColorProfilePrompt) {
    stateNameColorProfilePrompt(this);
}

TEST_F(StateNameTestSuite, ColorProfilePicker) {
    stateNameColorProfilePicker(this);
}

// ============================================
// FIREWALL DECRYPT ADDRESS TESTS
// ============================================

TEST_F(DecryptAddressTestSuite, AddressFormat) {
    decryptAddressFormat(this);
}

TEST_F(DecryptAddressTestSuite, Deterministic) {
    decryptAddressDeterministic(this);
}

TEST_F(DecryptAddressTestSuite, DecoyDiffers) {
    decryptDecoyDiffers(this);
}

TEST_F(DecryptAddressTestSuite, DecoySimilarity) {
    decryptDecoySimilarity(this);
}

TEST_F(DecryptAddressTestSuite, SetupRoundContainsTarget) {
    decryptSetupRoundContainsTarget(this);
}

// ============================================
// FIREWALL DECRYPT LIFECYCLE TESTS
// ============================================

TEST_F(DecryptLifecycleTestSuite, StartsInIntro) {
    decryptStartsInIntro(this);
}

TEST_F(DecryptLifecycleTestSuite, IntroTransitionsToScan) {
    decryptIntroTransitionsToScan(this);
}

TEST_F(DecryptLifecycleTestSuite, IntroShowsTitle) {
    decryptIntroShowsTitle(this);
}

TEST_F(DecryptLifecycleTestSuite, ScanShowsTarget) {
    decryptScanShowsTarget(this);
}

TEST_F(DecryptLifecycleTestSuite, CursorWraps) {
    decryptScanCursorWraps(this);
}

TEST_F(DecryptLifecycleTestSuite, CorrectAdvancesRound) {
    decryptCorrectAdvancesRound(this);
}

TEST_F(DecryptLifecycleTestSuite, WrongSelectionLoses) {
    decryptWrongSelectionLoses(this);
}

TEST_F(DecryptLifecycleTestSuite, AllRoundsWins) {
    decryptAllRoundsWins(this);
}

TEST_F(DecryptLifecycleTestSuite, WinSetsOutcome) {
    decryptWinSetsOutcome(this);
}

TEST_F(DecryptLifecycleTestSuite, LoseSetsOutcome) {
    decryptLoseSetsOutcome(this);
}

TEST_F(DecryptLifecycleTestSuite, StandaloneRestartAfterWin) {
    decryptStandaloneRestartAfterWin(this);
}

TEST_F(DecryptLifecycleTestSuite, StandaloneRestartAfterLose) {
    decryptStandaloneRestartAfterLose(this);
}

// ============================================
// FIREWALL DECRYPT MANAGED MODE TESTS
// ============================================

TEST_F(DecryptManagedTestSuite, LaunchesFromFdn) {
    decryptManagedLaunchesFromFdn(this);
}

TEST_F(DecryptManagedTestSuite, WinReturns) {
    decryptManagedWinReturns(this);
}

// ============================================
// FIREWALL DECRYPT CONFIG TESTS
// ============================================

TEST_F(DecryptConfigTestSuite, EasyPresetValues) {
    decryptEasyPresetValues(this);
}

TEST_F(DecryptConfigTestSuite, HardPresetValues) {
    decryptHardPresetValues(this);
}

// ============================================
// FIREWALL DECRYPT STATE NAME TESTS
// ============================================

TEST_F(DecryptStateNameTestSuite, NamesCorrect) {
    decryptStateNamesCorrect(this);
}

TEST_F(DecryptStateNameTestSuite, GetStateNameRoutes) {
    decryptGetStateNameRoutes(this);
}

// ============================================
// FIREWALL DECRYPT APP ID TESTS
// ============================================

TEST_F(DecryptAppIdTestSuite, AppIdForGame) {
    decryptAppIdForGame(this);
}

// ============================================
// E2E PROGRESSION TESTS
// ============================================

TEST_F(ProgressionE2ETestSuite, SignalEchoEasyWin) {
    e2eSignalEchoEasyWin(this);
}

TEST_F(ProgressionE2ETestSuite, AutoBoonOnSeventhButton) {
    e2eAutoBoonOnSeventhButton(this);
}

TEST_F(ProgressionE2ETestSuite, ServerSyncOnWin) {
    e2eServerSyncOnWin(this);
}

TEST_F(ProgressionE2ETestSuite, HardModeColorEquip) {
    e2eHardModeColorEquip(this);
}

TEST_F(ProgressionE2ETestSuite, ColorPromptDecline) {
    e2eColorPromptDecline(this);
}

TEST_F(ProgressionE2ETestSuite, ColorPickerFromIdle) {
    e2eColorPickerFromIdle(this);
}

TEST_F(ProgressionE2ETestSuite, EasyModeLoss) {
    e2eEasyModeLoss(this);
}

TEST_F(ProgressionE2ETestSuite, HardModeLoss) {
    e2eHardModeLoss(this);
}

TEST_F(ProgressionE2ETestSuite, FirewallDecryptProgression) {
    e2eFirewallDecryptProgression(this);
}

TEST_F(ProgressionE2ETestSuite, FirewallDecryptHard) {
    e2eFirewallDecryptHard(this);
}

TEST_F(ProgressionE2ETestSuite, NvsRoundTrip) {
    e2eNvsRoundTrip(this);
}

TEST_F(ProgressionE2ETestSuite, PromptAutoDismiss) {
    e2ePromptAutoDismiss(this);
}

TEST_F(ProgressionE2ETestSuite, DifficultyGatingDynamic) {
    e2eDifficultyGatingDynamic(this);
}

TEST_F(ProgressionE2ETestSuite, EquipLaterViaPicker) {
    e2eEquipLaterViaPicker(this);
}

// ============================================
// RE-ENCOUNTER TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, FirstEncounterLaunchesEasy) {
    firstEncounterLaunchesEasy(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterWithButtonShowsPrompt) {
    reencounterWithButtonShowsPrompt(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterChooseHardLaunchesHard) {
    reencounterChooseHardLaunchesHard(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterChooseEasyLaunchesRecreational) {
    reencounterChooseEasyLaunchesRecreational(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterChooseSkipReturnsToIdle) {
    reencounterChooseSkipReturnsToIdle(this);
}

TEST_F(NegativeFlowTestSuite, FullyCompletedReencounterAllRecreational) {
    fullyCompletedReencounterAllRecreational(this);
}

TEST_F(NegativeFlowTestSuite, ReencounterTimeoutDefaultsToSkip) {
    reencounterTimeoutDefaultsToSkip(this);
}

TEST_F(NegativeFlowTestSuite, RecreationalWinSkipsRewards) {
    recreationalWinSkipsRewards(this);
}

// ============================================
// COLOR PROMPT CONTEXT TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, ColorPromptFirstProfileShowsEquip) {
    colorPromptFirstProfileShowsEquip(this);
}

TEST_F(NegativeFlowTestSuite, ColorPromptWithExistingShowsSwap) {
    colorPromptWithExistingShowsSwap(this);
}

// ============================================
// COLOR PICKER BUTTON SWAP TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, PickerUpButtonEquipsProfile) {
    pickerUpButtonEquipsProfile(this);
}

TEST_F(NegativeFlowTestSuite, PickerDownButtonCyclesProfile) {
    pickerDownButtonCyclesProfile(this);
}

TEST_F(NegativeFlowTestSuite, PickerShowsRoleAwareDefaultName) {
    pickerShowsRoleAwareDefaultName(this);
}

// ============================================
// IDLE PALETTE INDICATOR TESTS
// ============================================

TEST_F(NegativeFlowTestSuite, IdleShowsPaletteIndicator) {
    idleShowsPaletteIndicator(this);
}

TEST_F(NegativeFlowTestSuite, IdleShowsEquippedPaletteName) {
    idleShowsEquippedPaletteName(this);
}

// ============================================
// BOUNTY ROLE TESTS
// ============================================

TEST_F(BountyFlowTestSuite, BountyFdnEasyWin) {
    bountyFdnEasyWin(this);
}

TEST_F(BountyFlowTestSuite, BountyFdnHardWinColorPrompt) {
    bountyFdnHardWinColorPrompt(this);
}

TEST_F(BountyFlowTestSuite, BountyReencounterPrompt) {
    bountyReencounterPrompt(this);
}

// ============================================
// GHOST RUNNER STUB TESTS
// ============================================

TEST_F(GhostRunnerStubTestSuite, StubIntroState) {
    ghostRunnerStubIntroState(this);
}

TEST_F(GhostRunnerStubTestSuite, StubAutoWin) {
    ghostRunnerStubAutoWin(this);
}

TEST_F(GhostRunnerStubTestSuite, StubManagedModeReturns) {
    ghostRunnerStubManagedModeReturns(this);
}

TEST_F(GhostRunnerStubTestSuite, StubStandaloneLoops) {
    ghostRunnerStubStandaloneLoops(this);
}

TEST_F(GhostRunnerStubTestSuite, StubFdnLaunch) {
    ghostRunnerStubFdnLaunch(this);
}

TEST_F(GhostRunnerStubTestSuite, StubAppIdRegistered) {
    ghostRunnerStubAppIdRegistered(this);
}

TEST_F(GhostRunnerStubTestSuite, StubStateNames) {
    ghostRunnerStubStateNames(this);
}

// ============================================
// SPIKE VECTOR STUB TESTS
// ============================================

TEST_F(SpikeVectorStubTestSuite, StubIntroState) {
    spikeVectorStubIntroState(this);
}

TEST_F(SpikeVectorStubTestSuite, StubAutoWin) {
    spikeVectorStubAutoWin(this);
}

TEST_F(SpikeVectorStubTestSuite, StubManagedModeReturns) {
    spikeVectorStubManagedModeReturns(this);
}

TEST_F(SpikeVectorStubTestSuite, StubStandaloneLoops) {
    spikeVectorStubStandaloneLoops(this);
}

TEST_F(SpikeVectorStubTestSuite, StubFdnLaunch) {
    spikeVectorStubFdnLaunch(this);
}

TEST_F(SpikeVectorStubTestSuite, StubAppIdRegistered) {
    spikeVectorStubAppIdRegistered(this);
}

TEST_F(SpikeVectorStubTestSuite, StubStateNames) {
    spikeVectorStubStateNames(this);
}

// ============================================
// CIPHER PATH TESTS
// ============================================

TEST_F(CipherPathTestSuite, EasyConfigPresets) {
    cipherPathEasyConfigPresets(this);
}

TEST_F(CipherPathTestSuite, HardConfigPresets) {
    cipherPathHardConfigPresets(this);
}

TEST_F(CipherPathTestSuite, IntroResetsSession) {
    cipherPathIntroResetsSession(this);
}

TEST_F(CipherPathTestSuite, IntroTransitionsToShow) {
    cipherPathIntroTransitionsToShow(this);
}

TEST_F(CipherPathTestSuite, ShowDisplaysRoundInfo) {
    cipherPathShowDisplaysRoundInfo(this);
}

TEST_F(CipherPathTestSuite, ShowGeneratesCipher) {
    cipherPathShowGeneratesCipher(this);
}

TEST_F(CipherPathTestSuite, ShowTransitionsToGameplay) {
    cipherPathShowTransitionsToGameplay(this);
}

TEST_F(CipherPathTestSuite, CorrectMoveAdvancesPosition) {
    cipherPathCorrectMoveAdvancesPosition(this);
}

TEST_F(CipherPathTestSuite, WrongMoveWastesMove) {
    cipherPathWrongMoveWastesMove(this);
}

TEST_F(CipherPathTestSuite, ReachExitTriggersEvaluate) {
    cipherPathReachExitTriggersEvaluate(this);
}

TEST_F(CipherPathTestSuite, BudgetExhaustedTriggersEvaluate) {
    cipherPathBudgetExhaustedTriggersEvaluate(this);
}

TEST_F(CipherPathTestSuite, EvaluateRoutesToNextRound) {
    cipherPathEvaluateRoutesToNextRound(this);
}

TEST_F(CipherPathTestSuite, EvaluateRoutesToWin) {
    cipherPathEvaluateRoutesToWin(this);
}

TEST_F(CipherPathTestSuite, EvaluateRoutesToLose) {
    cipherPathEvaluateRoutesToLose(this);
}

TEST_F(CipherPathTestSuite, WinSetsOutcome) {
    cipherPathWinSetsOutcome(this);
}

TEST_F(CipherPathTestSuite, LoseSetsOutcome) {
    cipherPathLoseSetsOutcome(this);
}

TEST_F(CipherPathTestSuite, StandaloneLoopsToIntro) {
    cipherPathStandaloneLoopsToIntro(this);
}

TEST_F(CipherPathTestSuite, StateNamesResolve) {
    cipherPathStateNamesResolve(this);
}

TEST_F(CipherPathManagedTestSuite, ManagedModeReturns) {
    cipherPathManagedModeReturns(this);
}

// ============================================
// EXPLOIT SEQUENCER STUB TESTS
// ============================================

TEST_F(ExploitSequencerStubTestSuite, StubIntroState) {
    exploitSequencerStubIntroState(this);
}

TEST_F(ExploitSequencerStubTestSuite, StubAutoWin) {
    exploitSequencerStubAutoWin(this);
}

TEST_F(ExploitSequencerStubTestSuite, StubManagedModeReturns) {
    exploitSequencerStubManagedModeReturns(this);
}

TEST_F(ExploitSequencerStubTestSuite, StubStandaloneLoops) {
    exploitSequencerStubStandaloneLoops(this);
}

TEST_F(ExploitSequencerStubTestSuite, StubFdnLaunch) {
    exploitSequencerStubFdnLaunch(this);
}

TEST_F(ExploitSequencerStubTestSuite, StubAppIdRegistered) {
    exploitSequencerStubAppIdRegistered(this);
}

TEST_F(ExploitSequencerStubTestSuite, StubStateNames) {
    exploitSequencerStubStateNames(this);
}

// ============================================
// BREACH DEFENSE STUB TESTS
// ============================================

TEST_F(BreachDefenseStubTestSuite, StubIntroState) {
    breachDefenseStubIntroState(this);
}

TEST_F(BreachDefenseStubTestSuite, StubAutoWin) {
    breachDefenseStubAutoWin(this);
}

TEST_F(BreachDefenseStubTestSuite, StubManagedModeReturns) {
    breachDefenseStubManagedModeReturns(this);
}

TEST_F(BreachDefenseStubTestSuite, StubStandaloneLoops) {
    breachDefenseStubStandaloneLoops(this);
}

TEST_F(BreachDefenseStubTestSuite, StubFdnLaunch) {
    breachDefenseStubFdnLaunch(this);
}

TEST_F(BreachDefenseStubTestSuite, StubAppIdRegistered) {
    breachDefenseStubAppIdRegistered(this);
}

TEST_F(BreachDefenseStubTestSuite, StubStateNames) {
    breachDefenseStubStateNames(this);
}

// ============================================
// MAIN
// ============================================

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
