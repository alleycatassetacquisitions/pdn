#pragma once

#include <gtest/gtest.h>

#include "rdc-hello-tests.hpp"
#include "rdc-tests.hpp"
#include "state/connect-state.hpp"
#include "state/state-lifecycle.hpp"

#include <vector>

// ============================================
// ConnectState per-jack connection callbacks (#165)
// ============================================
//
// A mounted ConnectState subscribes itself to the coordinator's jack observer
// and releases it on dismount. StateMachine dismounts the outgoing state before
// mounting the incoming one, so the single observer slot is handed from state to
// state without arbitration.
//
// These run on RDCHelloTests' fixture, which calls enableHelloConnectivity().
// That matters twice over: the link machine is the only emitter of jack edges,
// and the mount replay is gated on the same flag. No caller under src/ enables
// HELLO, so the shipping build delivers neither edges nor replays.

// Records every jack event it is handed, tagged with whether the state was mounted
// when it landed, which is what the replay-ordering tests assert on.
class RecordingConnectState : public ConnectState<Device> {
public:
    /// Binds the recorder to the coordinator whose jack events it captures.
    RecordingConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : ConnectState<Device>(remoteDeviceCoordinator, stateId) {}

    struct Event {
        SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK;
        bool connected = false;
        // mountedCount > dismountedCount when this landed, i.e. the state was
        // mounted — which for a replayed event means the mount hook had run.
        bool afterMountHook = false;
    };

    std::vector<Event> events;
    int mountedCount = 0;
    int dismountedCount = 0;

    /// Counts mounts so a replayed event can be shown to land after this ran.
    void onStateMounted(Device*) override { mountedCount++; }
    /// Counts dismounts; paired with mountedCount to tell mounted from not.
    void onStateDismounted(Device*) override { dismountedCount++; }

    /// Records the delivered event so the assertions can read it back.
    void onJackChange(SerialIdentifier jack, bool connected) override {
        Event event;
        event.jack = jack;
        event.connected = connected;
        event.afterMountHook = mountedCount > dismountedCount;
        events.push_back(event);
    }

protected:
    bool isPrimaryRequired() override { return true; }
    bool isAuxRequired() override { return true; }
};

// mount/dismount are private on State; StateMachine reaches them through
// StateLifecycle, and so do these tests.
inline void mountState(State* state, Device* device) {
    static_cast<StateLifecycle*>(state)->mount(device);
}
inline void dismountState(State* state, Device* device) {
    static_cast<StateLifecycle*>(state)->dismount(device);
}

/// RDCHelloTests::SetUp installs its own jack observer for connectCount /
/// disconnectCount. The slot is single and the setter overwrites it without
/// complaint, so a mounting ConnectState would silently detach the fixture's
/// counters; these tests hand the slot over deliberately instead.
class ConnectStateTests : public RDCHelloTests {
public:
    /// Builds the HELLO fixture, then hands the jack observer slot back.
    void SetUp() override {
        RDCHelloTests::SetUp();
        rdc.setOnJackChange(nullptr);
    }
};

// Drives the OUT jack through the real HELLO + context path to CONNECTED.
// A re-link needs a fresh seqId: the reliable channel drops a repeat of the last
// one it accepted from that sender, so the context would never reach the RDC.
inline void connectOutJack(RDCHelloTests* suite, uint8_t chainRole, uint16_t userId,
                           uint8_t seqId = 9) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    std::vector<uint8_t> ctx = pdnContextBytes(chainRole, userId, seqId);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
}

// A mounted state is handed the connect.
inline void connectStateMountedReceivesJackConnect(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    ASSERT_TRUE(state.events.empty()) << "nothing is connected yet";

    connectOutJack(suite, /*chainRole=*/2, /*userId=*/4242);

    ASSERT_EQ(state.events.size(), 1u);
    EXPECT_EQ(state.events[0].jack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_TRUE(state.events[0].connected);
}

// The disconnect reaches the state too, not just the connect.
inline void connectStateReceivesDisconnect(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    connectOutJack(suite, /*chainRole=*/2, /*userId=*/4242);
    ASSERT_EQ(state.events.size(), 1u);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    ASSERT_EQ(state.events.size(), 2u);
    EXPECT_EQ(state.events[1].jack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_FALSE(state.events[1].connected);
}

// Dismount releases the slot, so a dismounted state hears nothing further. This
// is what keeps a `this`-bound lambda from outliving the state's tenure.
inline void connectStateDismountedStopsReceiving(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    dismountState(&state, &suite->device);
    ASSERT_EQ(state.dismountedCount, 1);

    connectOutJack(suite, /*chainRole=*/2, /*userId=*/4242);

    EXPECT_TRUE(state.events.empty()) << "a dismounted state was still subscribed";
}

// A jack's connection is state, not an edge: a state mounted into an already
// cabled device is handed the connected jacks at mount rather than staying blind
// until the next physical event. The replay lands after the state's own mount
// hook, so it cannot reach half-initialized members.
inline void connectStateReplaysConnectedJackAtMount(RDCHelloTests* suite) {
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);

    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);

    ASSERT_EQ(state.events.size(), 1u) << "an already-connected jack was not replayed";
    EXPECT_EQ(state.events[0].jack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_TRUE(state.events[0].connected);
    EXPECT_TRUE(state.events[0].afterMountHook)
        << "the replay ran before the state's own mount hook";
}

// The replay gate is the port's live CONNECTED status, not "was ever connected":
// a jack whose link died is skipped, the same jack once revived is replayed. Both
// halves are needed — the empty case alone also passes if replay never runs.
inline void connectStateReplaysOnlyConnectedJacks(RDCHelloTests* suite) {
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK),
              PortStatus::DISCONNECTED);

    RecordingConnectState afterLinkDeath(&suite->rdc, /*stateId=*/1);
    mountState(&afterLinkDeath, &suite->device);
    EXPECT_TRUE(afterLinkDeath.events.empty())
        << "a disconnected jack was replayed as connected";

    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234, /*seqId=*/10);
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK),
              PortStatus::CONNECTED);

    RecordingConnectState afterRevival(&suite->rdc, /*stateId=*/2);
    mountState(&afterRevival, &suite->device);
    EXPECT_EQ(afterRevival.events.size(), 1u) << "the revived jack was not replayed";
}

// The chainRole goes with the link, so the next peer on that jack cannot inherit
// the departed one's. getPeerDeviceType is deliberately not asserted here: under
// HELLO it reads the quiesced handshake table and is UNKNOWN before the link dies
// as well as after, so the assertion could not fail.
inline void connectStatePeerFactsClearedOnDisconnect(RDCHelloTests* suite) {
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);
    ASSERT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), 4);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), 0);
}

// The replay is gated on HELLO because only the link machine emits jack edges.
// With the handshake driving connectivity the port reads CONNECTED, but replaying
// it would hand out a connect that no disconnect can ever follow.
inline void connectStateSkipsReplayWhenHelloOff(RDCTests* suite) {
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_FALSE(suite->rdc.isHelloConnectivityEnabled());
    ASSERT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);

    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);

    EXPECT_TRUE(state.events.empty())
        << "a handshake-connected jack was replayed as a HELLO edge";
}

// A state destroyed while still mounted — app teardown, which never dismounts —
// must not leave its `this`-bound lambda behind in the device-owned coordinator.
inline void connectStateDestructorReleasesSlot(RDCHelloTests* suite) {
    // Heap-allocated so ASan can prove the dispatch below would hit freed memory.
    // Only `-e native_asan` enforces that; under `-e native` this proves nothing.
    RecordingConnectState* state = new RecordingConnectState(&suite->rdc, /*stateId=*/1);
    mountState(state, &suite->device);
    delete state;

    connectOutJack(suite, /*chainRole=*/2, /*userId=*/4242);
    SUCCEED();
}
