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
// MAIN
// ============================================

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
