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
#include "challenge-protocol-tests.hpp"
#include "challenge-game-tests.hpp"
#include "cli-challenge-tests.hpp"
#include "device-mode-switch-tests.hpp"
#include "konami-progress-tests.hpp"
#include "profile-sync-tests.hpp"
#include "profile-portability-tests.hpp"
#include "signal-echo-tests.hpp"
#include "minigame-base-tests.hpp"
#include "cli-automation-tests.hpp"
#include "sm-manager-tests.hpp"
#include "integration-tests.hpp"

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
// CHALLENGE PROTOCOL TESTS
// ============================================

TEST_F(ChallengeProtocolTestSuite, ProtocolParseCdevMessage) {
    protocolParseCdevMessage(this);
}

TEST_F(ChallengeProtocolTestSuite, ProtocolParseCdevInvalid) {
    protocolParseCdevInvalid(this);
}

TEST_F(ChallengeProtocolTestSuite, ProtocolParseGresMessage) {
    protocolParseGresMessage(this);
}

TEST_F(ChallengeProtocolTestSuite, ProtocolGameTypeLookup) {
    protocolGameTypeLookup(this);
}

TEST_F(ChallengeProtocolTestSuite, MagicCodeDetectsChallenge) {
    magicCodeDetectsChallenge(this);
}

TEST_F(ChallengeProtocolTestSuite, MagicCodeRejectsPlayer) {
    magicCodeRejectsPlayer(this);
}

TEST_F(ChallengeProtocolTestSuite, MagicCodeMapsToGameType) {
    magicCodeMapsToGameType(this);
}

// ============================================
// CHALLENGE GAME TESTS
// ============================================

TEST_F(ChallengeGameTestSuite, NpcIdleBroadcastsCdev) {
    npcIdleBroadcastsCdev(this);
}

TEST_F(ChallengeGameTestSuite, NpcIdleDisplaysGameName) {
    npcIdleDisplaysGameName(this);
}

TEST_F(ChallengeGameTestSuite, NpcIdleTransitionsOnMac) {
    npcIdleTransitionsOnMac(this);
}

TEST_F(ChallengeGameTestSuite, NpcHandshakeSendsCack) {
    npcHandshakeSendsCack(this);
}

TEST_F(ChallengeGameTestSuite, NpcReceiveResultShowsOutcome) {
    npcReceiveResultShowsOutcome(this);
}

TEST_F(ChallengeGameTestSuite, NpcReceiveResultCachesResult) {
    npcReceiveResultCachesResult(this);
}

TEST_F(ChallengeGameTestSuite, NpcReceiveResultTransitions) {
    npcReceiveResultTransitions(this);
}

TEST_F(ChallengeGameTestSuite, NpcLedAnimationPlays) {
    npcLedAnimationPlays(this);
}

// ============================================
// CLI CHALLENGE FACTORY TESTS
// ============================================

TEST_F(CliChallengeTestSuite, CliFactoryCreatesChallenge) {
    cliFactoryCreatesChallenge(this);
}

TEST_F(CliChallengeTestSuite, CliFactoryChallengeHasCorrectGame) {
    cliFactoryChallengeHasCorrectGame(this);
}

TEST_F(CliChallengeTestSuite, UniqueCodesInTestFixture) {
    uniqueCodesInTestFixture(this);
}

// ============================================
// DEVICE MODE SWITCH TESTS
// ============================================

TEST_F(DeviceModeSwitchTestSuite, NpcModePersistsAcrossReboot) {
    npcModePersistsAcrossReboot(this);
}

TEST_F(DeviceModeSwitchTestSuite, ClearedModeFallsBackToQuickdraw) {
    clearedModeFallsBackToQuickdraw(this);
}

TEST_F(DeviceModeSwitchTestSuite, SpecialCodesStillWork) {
    specialCodesStillWork(this);
}

TEST_F(DeviceModeSwitchTestSuite, NpcToPlayerToNpc) {
    npcToPlayerToNpc(this);
}

TEST_F(DeviceModeSwitchTestSuite, PlayerToNpcToPlayer) {
    playerToNpcToPlayer(this);
}

TEST_F(DeviceModeSwitchTestSuite, FetchUserDetectsChallengeCode) {
    fetchUserDetectsChallengeCode(this);
}

// ============================================
// PROFILE SYNC TESTS
// ============================================

TEST_F(ProfileSyncTestSuite, ServerReturnsProgress) {
    profileSyncServerReturnsProgress(this);
}

TEST_F(ProfileSyncTestSuite, FetchUserDataParsesProgress) {
    profileSyncFetchUserDataParsesProgress(this);
}

TEST_F(ProfileSyncTestSuite, FetchUserDataMissingProgress) {
    profileSyncFetchUserDataMissingProgress(this);
}

TEST_F(ProfileSyncTestSuite, ProgressSavedToNVS) {
    profileSyncProgressSavedToNVS(this);
}

TEST_F(ProfileSyncTestSuite, ProgressLoadedFromNVS) {
    profileSyncProgressLoadedFromNVS(this);
}

TEST_F(ProfileSyncTestSuite, ProgressUploadSendsCorrectJson) {
    profileSyncProgressUploadSendsCorrectJson(this);
}

// ============================================
// KONAMI PROGRESS TESTS
// ============================================

TEST_F(KonamiProgressTestSuite, UnlockSingleButton) {
    konamiUnlockSingleButton(this);
}

TEST_F(KonamiProgressTestSuite, UnlockMultipleButtons) {
    konamiUnlockMultipleButtons(this);
}

TEST_F(KonamiProgressTestSuite, HasUnlockedButtonTrue) {
    konamiHasUnlockedButtonTrue(this);
}

TEST_F(KonamiProgressTestSuite, HasUnlockedButtonFalse) {
    konamiHasUnlockedButtonFalse(this);
}

TEST_F(KonamiProgressTestSuite, AllButtonsUnlocked) {
    konamiAllButtonsUnlocked(this);
}

TEST_F(KonamiProgressTestSuite, DuplicateUnlockIdempotent) {
    konamiDuplicateUnlockIdempotent(this);
}

TEST_F(KonamiProgressTestSuite, ProgressSerializesToJson) {
    konamiProgressSerializesToJson(this);
}

TEST_F(KonamiProgressTestSuite, ProgressDeserializesFromJson) {
    konamiProgressDeserializesFromJson(this);
}

TEST_F(KonamiProgressTestSuite, ProgressDeserializeMissingField) {
    konamiProgressDeserializeMissingField(this);
}

// ============================================
// PROFILE PORTABILITY TESTS
// ============================================

TEST_F(ProfilePortabilityTestSuite, LoginFetchesProfile) {
    portabilityLoginFetchesProfile(this);
}

TEST_F(ProfilePortabilityTestSuite, LoginSameDeviceSameProfile) {
    portabilityLoginSameDeviceSameProfile(this);
}

TEST_F(ProfilePortabilityTestSuite, LoginDifferentDeviceSameProfile) {
    portabilityLoginDifferentDeviceSameProfile(this);
}

TEST_F(ProfilePortabilityTestSuite, LoginNewProfileOnReassignedDevice) {
    portabilityLoginNewProfileOnReassignedDevice(this);
}

TEST_F(ProfilePortabilityTestSuite, BoothCheckInOutChain) {
    portabilityBoothCheckInOutChain(this);
}

TEST_F(ProfilePortabilityTestSuite, OfflineFallbackToLocalProgress) {
    portabilityOfflineFallbackToLocalProgress(this);
}

TEST_F(ProfilePortabilityTestSuite, ProgressSurvivesRestart) {
    portabilityProgressSurvivesRestart(this);
}

// ============================================
// MINIGAME BASE CLASS TESTS
// ============================================

TEST_F(MiniGameTestSuite, MiniGameBaseIdentity) {
    miniGameBaseIdentity(this);
}

TEST_F(MiniGameTestSuite, MiniGameBaseOutcomeDefault) {
    miniGameBaseOutcomeDefault(this);
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

TEST_F(SignalEchoTestSuite, ShowSequenceTimingPerSignal) {
    echoShowSequenceTimingPerSignal(this);
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

TEST_F(SignalEchoDifficultyTestSuite, EasyModeSequenceLength4) {
    echoDiffEasyModeSequenceLength4(this);
}

TEST_F(SignalEchoDifficultyTestSuite, EasyMode3MistakesAllowed) {
    echoDiffEasyMode3MistakesAllowed(this);
}

TEST_F(SignalEchoDifficultyTestSuite, HardModeSequenceLength8) {
    echoDiffHardModeSequenceLength8(this);
}

TEST_F(SignalEchoDifficultyTestSuite, HardMode1MistakeAllowed) {
    echoDiffHardMode1MistakeAllowed(this);
}

TEST_F(SignalEchoDifficultyTestSuite, WrongInputShowsErrorMarker) {
    echoDiffWrongInputShowsErrorMarker(this);
}

TEST_F(SignalEchoDifficultyTestSuite, WrongInputAdvancesToNext) {
    echoDiffWrongInputAdvancesToNext(this);
}

TEST_F(SignalEchoDifficultyTestSuite, LifeIndicatorStartsFull) {
    echoDiffLifeIndicatorStartsFull(this);
}

TEST_F(SignalEchoDifficultyTestSuite, LifeIndicatorDecrementsOnMistake) {
    echoDiffLifeIndicatorDecrementsOnMistake(this);
}

TEST_F(SignalEchoDifficultyTestSuite, LifeIndicatorCorrectCount) {
    echoDiffLifeIndicatorCorrectCount(this);
}

TEST_F(SignalEchoDifficultyTestSuite, FullResetOnLose) {
    echoDiffFullResetOnLose(this);
}

TEST_F(SignalEchoDifficultyTestSuite, EasyModeWinUnlocksKonami) {
    echoDiffEasyModeWinUnlocksKonami(this);
}

TEST_F(SignalEchoDifficultyTestSuite, HardModeWinUnlocksColorProfile) {
    echoDiffHardModeWinUnlocksColorProfile(this);
}

TEST_F(SignalEchoDifficultyTestSuite, MistakeIsRegistered) {
    echoDiffMistakeIsRegistered(this);
}

// ============================================
// COLOR PROFILE TESTS
// ============================================

TEST_F(ColorProfileTestSuite, SignalEchoPaletteValues) {
    colorProfileSignalEchoPaletteValues(this);
}

TEST_F(ColorProfileTestSuite, IdlePaletteSize) {
    colorProfileIdlePaletteSize(this);
}

TEST_F(ColorProfileTestSuite, DefaultPaletteValues) {
    colorProfileDefaultPaletteValues(this);
}

TEST_F(ColorProfileTestSuite, IdleStateHasBrightness) {
    colorProfileIdleStateHasBrightness(this);
}

TEST_F(ColorProfileTestSuite, WinStateIsRainbow) {
    colorProfileWinStateIsRainbow(this);
}

TEST_F(ColorProfileTestSuite, LoseStateIsRed) {
    colorProfileLoseStateIsRed(this);
}

TEST_F(ColorProfileTestSuite, ErrorStateIsRed) {
    colorProfileErrorStateIsRed(this);
}

TEST_F(ColorProfileTestSuite, CorrectStateIsGreen) {
    colorProfileCorrectStateIsGreen(this);
}

// ============================================
// EVENT LOGGER TESTS
// ============================================

TEST_F(EventLoggerTestSuite, LogAndRetrieve) {
    eventLoggerLogAndRetrieve(this);
}

TEST_F(EventLoggerTestSuite, FilterByType) {
    eventLoggerFilterByType(this);
}

TEST_F(EventLoggerTestSuite, FilterByDevice) {
    eventLoggerFilterByDevice(this);
}

TEST_F(EventLoggerTestSuite, FilterByTypeAndDevice) {
    eventLoggerFilterByTypeAndDevice(this);
}

TEST_F(EventLoggerTestSuite, GetLast) {
    eventLoggerGetLast(this);
}

TEST_F(EventLoggerTestSuite, HasEvent) {
    eventLoggerHasEvent(this);
}

TEST_F(EventLoggerTestSuite, Count) {
    eventLoggerCount(this);
}

TEST_F(EventLoggerTestSuite, Clear) {
    eventLoggerClear(this);
}

TEST_F(EventLoggerTestSuite, FormatEvent) {
    eventLoggerFormatEvent(this);
}

TEST_F(EventLoggerTestSuite, WriteToFile) {
    eventLoggerWriteToFile(this);
}

// ============================================
// DRIVER CALLBACK TESTS
// ============================================

TEST_F(DriverCallbackTestSuite, SerialWriteCallbackFires) {
    serialDriverWriteCallbackFires(this);
}

TEST_F(DriverCallbackTestSuite, SerialWriteCallbackCharPtr) {
    serialDriverWriteCallbackCharPtr(this);
}

TEST_F(DriverCallbackTestSuite, SerialReadCallbackFires) {
    serialDriverReadCallbackFires(this);
}

TEST_F(DriverCallbackTestSuite, CallbacksCoexistWithStringCallback) {
    serialDriverCallbacksCoexistWithStringCallback(this);
}

TEST_F(DriverCallbackTestSuite, DisplayTextCallbackFires) {
    displayDriverTextCallbackFires(this);
}

TEST_F(DriverCallbackTestSuite, DisplayTextCallbackPositioned) {
    displayDriverTextCallbackPositioned(this);
}

TEST_F(DriverCallbackTestSuite, DisplayTextCallbackMultipleCalls) {
    displayDriverTextCallbackMultipleCalls(this);
}

// ============================================
// SCRIPT RUNNER TESTS
// ============================================

TEST_F(ScriptRunnerTestSuite, ParseSkipsComments) {
    scriptRunnerParseSkipsComments(this);
}

TEST_F(ScriptRunnerTestSuite, ExecutesCableCommand) {
    scriptRunnerExecutesCableCommand(this);
}

TEST_F(ScriptRunnerTestSuite, WaitAdvancesClock) {
    scriptRunnerWaitAdvancesClock(this);
}

TEST_F(ScriptRunnerTestSuite, TickRunsLoops) {
    scriptRunnerTickRunsLoops(this);
}

TEST_F(ScriptRunnerTestSuite, PressButton) {
    scriptRunnerPressButton(this);
}

TEST_F(ScriptRunnerTestSuite, AssertStatePass) {
    scriptRunnerAssertStatePass(this);
}

TEST_F(ScriptRunnerTestSuite, AssertStateFail) {
    scriptRunnerAssertStateFail(this);
}

TEST_F(ScriptRunnerTestSuite, AssertTextPass) {
    scriptRunnerAssertTextPass(this);
}

TEST_F(ScriptRunnerTestSuite, AssertSerialTx) {
    scriptRunnerAssertSerialTx(this);
}

TEST_F(ScriptRunnerTestSuite, AssertNoEvent) {
    scriptRunnerAssertNoEvent(this);
}

TEST_F(ScriptRunnerTestSuite, StopsOnFirstError) {
    scriptRunnerStopsOnFirstError(this);
}

TEST_F(ScriptRunnerTestSuite, ReportsLineNumber) {
    scriptRunnerReportsLineNumber(this);
}

// ============================================
// SM MANAGER TESTS
// ============================================

TEST_F(SmManagerTestSuite, smManagerDefaultLoop) {
    smManagerDefaultLoop(this);
}

TEST_F(SmManagerTestSuite, smManagerPauseAndLoad) {
    smManagerPauseAndLoad(this);
}

TEST_F(SmManagerTestSuite, smManagerAutoResume) {
    smManagerAutoResume(this);
}

TEST_F(SmManagerTestSuite, smManagerOutcomeStored) {
    smManagerOutcomeStored(this);
}

TEST_F(SmManagerTestSuite, smManagerResumeToCorrectState) {
    smManagerResumeToCorrectState(this);
}

TEST_F(SmManagerTestSuite, smManagerDeletesSwappedGame) {
    smManagerDeletesSwappedGame(this);
}

TEST_F(SmManagerTestSuite, smManagerPauseAndLoadWon) {
    smManagerPauseAndLoadWon(this);
}

TEST_F(SmManagerTestSuite, smManagerPauseAndLoadLost) {
    smManagerPauseAndLoadLost(this);
}

// ============================================
// FEATURE A/B/E VALIDATION TESTS
// ============================================

TEST_F(ScriptRunnerTestSuite, FeatureA_NpcBroadcastsCdev) {
    featureA_NpcBroadcastsCdev(this);
}

TEST_F(ScriptRunnerTestSuite, FeatureA_CdevMessageFormat) {
    featureA_CdevMessageFormat(this);
}

TEST_F(ScriptRunnerTestSuite, FeatureB_SignalEchoStandaloneWin) {
    featureB_SignalEchoStandaloneWin(this);
}

TEST_F(ScriptRunnerTestSuite, FeatureB_SignalEchoStandaloneLose) {
    featureB_SignalEchoStandaloneLose(this);
}

TEST_F(ScriptRunnerTestSuite, FeatureE_KonamiProgressTracking) {
    featureE_KonamiProgressTracking(this);
}

// ============================================
// INTEGRATION TESTS
// ============================================

TEST_F(IntegrationTestSuite, CdevTriggersTransition) {
    integrationCdevTriggersTransition(this);
}

TEST_F(IntegrationTestSuite, ChallengeDetectedSendsMac) {
    integrationChallengeDetectedSendsMac(this);
}

TEST_F(IntegrationTestSuite, SwapsOnCack) {
    integrationSwapsOnCack(this);
}

TEST_F(IntegrationTestSuite, TimeoutToIdle) {
    integrationTimeoutToIdle(this);
}

TEST_F(IntegrationTestSuite, InvalidCdevToIdle) {
    integrationInvalidCdevToIdle(this);
}

TEST_F(IntegrationTestSuite, ChallengeCompleteWinToIdle) {
    integrationChallengeCompleteWinToIdle(this);
}

TEST_F(IntegrationTestSuite, ChallengeCompleteUnlocksKonami) {
    integrationChallengeCompleteUnlocksKonami(this);
}

TEST_F(IntegrationTestSuite, NoUnlockOnLoss) {
    integrationNoUnlockOnLoss(this);
}

TEST_F(IntegrationTestSuite, SavesProgress) {
    integrationSavesProgress(this);
}

TEST_F(IntegrationTestSuite, SmManagerWired) {
    integrationSmManagerWired(this);
}

TEST_F(IntegrationTestSuite, PlayerPendingChallenge) {
    integrationPlayerPendingChallenge(this);
}

TEST_F(IntegrationTestSuite, NormalHandshakeStillWorks) {
    integrationNormalHandshakeStillWorks(this);
}

// ============================================
// MAIN
// ============================================

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
