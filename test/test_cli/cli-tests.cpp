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
// MAIN
// ============================================

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
