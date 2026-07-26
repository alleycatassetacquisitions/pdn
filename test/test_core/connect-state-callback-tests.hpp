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
        bool hadContext = false;
        DeviceType peerType = DeviceType::UNKNOWN;
        uint8_t chainRole = 0;
        std::vector<uint8_t> profile;
    };

    std::vector<Event> events;
    int mountedCount = 0;
    int dismountedCount = 0;

    /// Registers the recorder and counts mounts, so a replayed event can be shown
    /// to land after this ran. Registration belongs here: the mount replay runs
    /// after the subclass's own mount hook.
    void onStateMounted(Device*) override {
        mountedCount++;
        setOnJackChange([this](SerialIdentifier jack, const JackConnectionState& state) {
            record(jack, state);
        });
    }
    /// Counts dismounts; paired with mountedCount to tell mounted from not.
    void onStateDismounted(Device*) override { dismountedCount++; }

protected:
    bool isPrimaryRequired() override { return true; }
    bool isAuxRequired() override { return true; }

private:
    void record(SerialIdentifier jack, const JackConnectionState& state) {
        Event event;
        event.jack = jack;
        event.connected = state.connected;
        event.afterMountHook = mountedCount > dismountedCount;
        event.hadContext = state.context.has_value();
        if (state.context.has_value()) {
            event.peerType = state.context->peerType;
            event.chainRole = state.context->chainRole;
            event.profile.assign(state.context->profile.begin(),
                                 state.context->profile.begin() + state.context->profileLen);
        }
        events.push_back(event);
    }
};

// Registers from its constructor, which is the shape the #145 design sketch uses.
// Counts only, because what is under test is that one registration reaches every
// tenure rather than what the payload carries.
class ConstructorRegisteredConnectState : public ConnectState<Device> {
public:
    /// Binds the coordinator and registers the handler in one step.
    ConstructorRegisteredConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                      int stateId)
        : ConnectState<Device>(remoteDeviceCoordinator, stateId) {
        setOnJackChange([this](SerialIdentifier, const JackConnectionState& state) {
            if (state.connected) {
                connects++;
            } else {
                disconnects++;
            }
        });
    }

    int connects = 0;
    int disconnects = 0;

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

// The connect carries the peer context: kind, chainRole and the opaque profile,
// all from the exchange. This is what lets a state read the peer's game role at
// connection time instead of polling for it.
inline void connectStateConnectCarriesPeerContext(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);

    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);

    ASSERT_EQ(state.events.size(), 1u);
    ASSERT_TRUE(state.events[0].hadContext) << "the connect arrived without a context";
    EXPECT_EQ(state.events[0].peerType, DeviceType::PDN);
    EXPECT_EQ(state.events[0].chainRole, 4);
    ASSERT_EQ(state.events[0].profile.size(), sizeof(PlayerProfile));
    PlayerProfile decoded{};
    memcpy(&decoded, state.events[0].profile.data(), sizeof(decoded));
    EXPECT_EQ(decoded.userId, 1234) << "the profile bytes were not the peer's";
}

// A mount after the context already landed still gets it, because the context is
// read at dispatch rather than captured by whoever was mounted when it arrived.
inline void connectStateReplayCarriesPeerContext(RDCHelloTests* suite) {
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);

    RecordingConnectState late(&suite->rdc, /*stateId=*/1);
    mountState(&late, &suite->device);

    ASSERT_EQ(late.events.size(), 1u);
    ASSERT_TRUE(late.events[0].hadContext) << "a late mount lost the peer context";
    EXPECT_EQ(late.events[0].peerType, DeviceType::PDN);
    EXPECT_EQ(late.events[0].chainRole, 4);
}

// A disconnect carries no context, so a handler cannot mistake the departed peer's
// facts — which the link is still holding at that instant — for a live peer's.
inline void connectStateDisconnectCarriesNoContext(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);
    ASSERT_EQ(state.events.size(), 1u);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    ASSERT_EQ(state.events.size(), 2u);
    EXPECT_FALSE(state.events[1].connected);
    EXPECT_FALSE(state.events[1].hadContext);
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

// The peer facts go with the link, so the next peer on that jack cannot inherit
// the departed one's. Each fact is asserted set before it is asserted cleared,
// or "cleared" would also pass against a fact that was never stored.
inline void connectStatePeerFactsClearedOnDisconnect(RDCHelloTests* suite) {
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);
    ASSERT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), 4);
    ASSERT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::PDN);
    size_t profileLen = 0;
    ASSERT_NE(suite->rdc.getPeerProfile(SerialIdentifier::OUTPUT_JACK, profileLen), nullptr);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), 0);
    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK),
              DeviceType::UNKNOWN);
    EXPECT_EQ(suite->rdc.getPeerProfile(SerialIdentifier::OUTPUT_JACK, profileLen), nullptr);
    EXPECT_EQ(profileLen, 0u);
}

// One registration made in the constructor reaches the mount replay, the live
// disconnect, and a second tenure after a dismount/remount. This is the usage the
// #145 sketch documents, so it has to hold without re-registering per mount.
inline void connectStateConstructorRegistrationCoversEveryTenure(RDCHelloTests* suite) {
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);

    ConstructorRegisteredConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    ASSERT_EQ(state.connects, 1) << "the mount replay missed a constructor registration";

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(state.disconnects, 1) << "the live disconnect missed it";

    dismountState(&state, &suite->device);
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234, /*seqId=*/10);
    mountState(&state, &suite->device);

    EXPECT_EQ(state.connects, 2) << "the registration did not survive a remount";
}

// A registered handler stops firing once it is cleared, so a state can drop an
// observer whose captures are about to die.
inline void connectStateClearedHandlerStopsReceiving(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    connectOutJack(suite, /*chainRole=*/4, /*userId=*/1234);
    ASSERT_EQ(state.events.size(), 1u);

    state.setOnJackChange(nullptr);
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(state.events.size(), 1u) << "a cleared handler still fired";
}

// The peer's kind comes from the context channel that decoded it, not the HELLO
// deviceType byte. The two travel over different transports and can disagree, and
// a consumer casting the profile bytes needs the kind that produced them. A swap
// leaves the jack UNKNOWN until the new peer's own context lands.
inline void rdcHelloPeerDeviceTypeComesFromContextChannel(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    HelloPayload lying{};
    memcpy(lying.source, peer, 6);
    lying.deviceType = static_cast<uint8_t>(DeviceType::FDN);
    suite->deliverHello(suite->outJack, encodeFramed(lying));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::UNKNOWN)
        << "the HELLO byte set the kind";

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/4, /*userId=*/1234, /*seqId=*/9);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::PDN)
        << "the kind did not come from the channel that decoded the context";

    HelloPayload swapped{};
    swapped.source[0] = 0xB1;
    swapped.source[1] = 0x02;
    swapped.source[2] = 0x03;
    swapped.source[3] = 0x04;
    swapped.source[4] = 0x05;
    swapped.source[5] = 0x06;
    swapped.deviceType = static_cast<uint8_t>(DeviceType::PDN);
    suite->deliverHello(suite->outJack, encodeFramed(swapped));
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::UNKNOWN)
        << "the departed peer's kind survived the swap";
}

// The replay is gated on HELLO because only the link machine emits jack edges.
// With the handshake driving connectivity the port reads CONNECTED, but replaying
// it would hand out a connect that no disconnect can ever follow.
inline void connectStateSkipsReplayWhenHelloOff(RDCTests* suite) {
    suite->device.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    suite->rdc.sync(&suite->device);
    suite->deliverPacketViaRDC(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE)
        << "no link machine, so no edge source for this jack";
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
