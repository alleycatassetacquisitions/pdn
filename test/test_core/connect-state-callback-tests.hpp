#pragma once

#include <gtest/gtest.h>

#include "rdc-hello-tests.hpp"
#include "state/connect-state.hpp"
#include "state/state-lifecycle.hpp"

#include <vector>

// ============================================
// ConnectState per-jack connection callbacks (#165)
// ============================================
//
// A mounted ConnectState subscribes itself to the coordinator's jack observer
// and removes its registration on dismount.
//
// These run on RDCHelloTests' fixture. The link machine is the only emitter of
// jack edges, and the mount replay reads the same link state.

// Records every jack event it is handed, tagged with whether the state was mounted
// when it landed, which is what the replay-ordering tests assert on.
class RecordingConnectState : public ConnectState<Device> {
public:
    /** Binds the recorder to the coordinator whose jack events it captures. */
    RecordingConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : ConnectState<Device>(remoteDeviceCoordinator, stateId) {}

    struct Event {
        SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK;
        bool connected = false;
        // mountedCount > dismountedCount when this landed, i.e. the state was
        // mounted — which for a replayed event means the mount hook had run.
        bool afterMountHook = false;
        bool hadContext = false;
        // What the state's own isConnected() answered inside the handler.
        bool stateReadConnected = false;
        DeviceType peerType = DeviceType::UNKNOWN;
        ChainRole chainRole = ChainRole::STANDALONE;
        std::vector<uint8_t> profile;
    };

    std::vector<Event> events;
    int mountedCount = 0;
    int dismountedCount = 0;

    /**
     * Registers the recorder and counts mounts, so a replayed event can be shown
     * to land after this ran. Registration belongs here: the mount replay runs
     * after the subclass's own mount hook.
     */
    void onStateMounted(Device*) override {
        mountedCount++;
        setOnJackState([this](SerialIdentifier jack, const JackConnectionState& state) {
            record(jack, state);
        });
    }
    /** Counts dismounts; paired with mountedCount to tell mounted from not. */
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
        event.stateReadConnected = isConnected();
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
    /** Binds the coordinator and registers the handler in one step. */
    ConstructorRegisteredConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                      int stateId)
        : ConnectState<Device>(remoteDeviceCoordinator, stateId) {
        setOnJackState([this](SerialIdentifier, const JackConnectionState& state) {
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

// The fixture keeps its own jack observer for connectCount / disconnectCount;
// a mounted ConnectState registers beside it under its own key.
class ConnectStateTests : public RDCHelloTests {};

// Drives the OUT jack through the real HELLO + context path to CONNECTED.
// A re-link needs a fresh seqId: the reliable channel drops a repeat of the last
// one it accepted from that sender, so the context would never reach the RDC.
inline void connectOutJack(RDCHelloTests* suite, ChainRole chainRole, uint16_t userId,
                           uint8_t seqId = 9) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
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

    connectOutJack(suite, ChainRole::CHILD, /*userId=*/4242);

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

    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);

    ASSERT_EQ(state.events.size(), 1u);
    ASSERT_TRUE(state.events[0].hadContext) << "the connect arrived without a context";
    EXPECT_EQ(state.events[0].peerType, DeviceType::PDN);
    EXPECT_EQ(state.events[0].chainRole, ChainRole::RING);
    ASSERT_EQ(state.events[0].profile.size(), sizeof(PlayerProfile));
    PlayerProfile decoded{};
    memcpy(&decoded, state.events[0].profile.data(), sizeof(decoded));
    EXPECT_EQ(decoded.userId, 1234) << "the profile bytes were not the peer's";
}

// A mount after the context already landed still gets it, because the context is
// read at dispatch rather than captured by whoever was mounted when it arrived.
inline void connectStateReplayCarriesPeerContext(RDCHelloTests* suite) {
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);

    RecordingConnectState late(&suite->rdc, /*stateId=*/1);
    mountState(&late, &suite->device);

    ASSERT_EQ(late.events.size(), 1u);
    ASSERT_TRUE(late.events[0].hadContext) << "a late mount lost the peer context";
    EXPECT_EQ(late.events[0].peerType, DeviceType::PDN);
    EXPECT_EQ(late.events[0].chainRole, ChainRole::RING);
}

// A disconnect carries no context, so a handler cannot mistake the departed peer's
// facts — which the link is still holding at that instant — for a live peer's.
inline void connectStateDisconnectCarriesNoContext(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);
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
    connectOutJack(suite, ChainRole::CHILD, /*userId=*/4242);
    ASSERT_EQ(state.events.size(), 1u);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    ASSERT_EQ(state.events.size(), 2u);
    EXPECT_EQ(state.events[1].jack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_FALSE(state.events[1].connected);
}

// isConnected() is answered from the edges a mounted state has been handed, and
// already reflects each one when the state's handler runs.
inline void connectStateIsConnectedFollowsJackEdges(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    EXPECT_FALSE(state.isConnected());

    connectOutJack(suite, ChainRole::CHILD, /*userId=*/4242);
    EXPECT_TRUE(state.isConnected());

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    EXPECT_FALSE(state.isConnected());

    ASSERT_EQ(state.events.size(), 2u);
    EXPECT_TRUE(state.events[0].stateReadConnected);
    EXPECT_FALSE(state.events[1].stateReadConnected)
    << "the handler saw the departed peer's jack as still connected";

    dismountState(&state, &suite->device);
}

// What a tenure learned goes with it, and the next mount's replay rebuilds it.
inline void connectStateForgetsItsJacksBetweenTenures(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    EXPECT_FALSE(state.isConnected()) << "a state that was never mounted read as cabled";

    connectOutJack(suite, ChainRole::CHILD, /*userId=*/4242);
    mountState(&state, &suite->device);
    EXPECT_TRUE(state.isConnected()) << "the mount replay did not reach isConnected()";

    dismountState(&state, &suite->device);
    EXPECT_FALSE(state.isConnected()) << "an unmounted state still read as cabled";

    mountState(&state, &suite->device);
    EXPECT_TRUE(state.isConnected());
    dismountState(&state, &suite->device);
}

// Dismount removes the registration, so a dismounted state hears nothing
// further. This is what keeps a `this`-bound lambda from outliving the state's
// tenure.
inline void connectStateDismountedStopsReceiving(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    dismountState(&state, &suite->device);
    ASSERT_EQ(state.dismountedCount, 1);

    connectOutJack(suite, ChainRole::CHILD, /*userId=*/4242);

    EXPECT_TRUE(state.events.empty()) << "a dismounted state was still subscribed";
}

// A jack's connection is state, not an edge: a state mounted into an already
// cabled device is handed the connected jacks at mount rather than staying blind
// until the next physical event. The replay lands after the state's own mount
// hook, so it cannot reach half-initialized members.
inline void connectStateReplaysConnectedJackAtMount(RDCHelloTests* suite) {
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);

    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);

    ASSERT_EQ(state.events.size(), 1u) << "an already-connected jack was not replayed";
    EXPECT_EQ(state.events[0].jack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_TRUE(state.events[0].connected);
    EXPECT_TRUE(state.events[0].afterMountHook)
        << "the replay ran before the state's own mount hook";
}

// The replay gate is the jack's live CONNECTED status, not "was ever connected":
// a jack whose link died is skipped, the same jack once revived is replayed. Both
// halves are needed — the empty case alone also passes if replay never runs.
inline void connectStateReplaysOnlyConnectedJacks(RDCHelloTests* suite) {
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK),
              JackStatus::DISCONNECTED);

    RecordingConnectState afterLinkDeath(&suite->rdc, /*stateId=*/1);
    mountState(&afterLinkDeath, &suite->device);
    EXPECT_TRUE(afterLinkDeath.events.empty())
        << "a disconnected jack was replayed as connected";

    connectOutJack(suite, ChainRole::RING, /*userId=*/1234, /*seqId=*/10);
    ASSERT_EQ(suite->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK),
              JackStatus::CONNECTED);

    RecordingConnectState afterRevival(&suite->rdc, /*stateId=*/2);
    mountState(&afterRevival, &suite->device);
    EXPECT_EQ(afterRevival.events.size(), 1u) << "the revived jack was not replayed";
}

// The peer facts go with the link, so the next peer on that jack cannot inherit
// the departed one's. Each fact is asserted set before it is asserted cleared,
// or "cleared" would also pass against a fact that was never stored.
inline void connectStatePeerFactsClearedOnDisconnect(RDCHelloTests* suite) {
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);
    ASSERT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), ChainRole::RING);
    ASSERT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::PDN);
    size_t profileLen = 0;
    ASSERT_NE(suite->rdc.getPeerProfile(SerialIdentifier::OUTPUT_JACK, profileLen), nullptr);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), ChainRole::STANDALONE);
    EXPECT_EQ(suite->rdc.getPeerDeviceType(SerialIdentifier::OUTPUT_JACK),
              DeviceType::UNKNOWN);
    EXPECT_EQ(suite->rdc.getPeerProfile(SerialIdentifier::OUTPUT_JACK, profileLen), nullptr);
    EXPECT_EQ(profileLen, 0u);
}

// One registration made in the constructor reaches the mount replay, the live
// disconnect, and a second tenure after a dismount/remount. This is the usage the
// #145 sketch documents, so it has to hold without re-registering per mount.
inline void connectStateConstructorRegistrationCoversEveryTenure(RDCHelloTests* suite) {
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);

    ConstructorRegisteredConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    ASSERT_EQ(state.connects, 1) << "the mount replay missed a constructor registration";

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(state.disconnects, 1) << "the live disconnect missed it";

    dismountState(&state, &suite->device);
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234, /*seqId=*/10);
    mountState(&state, &suite->device);

    EXPECT_EQ(state.connects, 2) << "the registration did not survive a remount";
}

// A registered handler stops firing once it is cleared, so a state can drop an
// observer whose captures are about to die.
inline void connectStateClearedHandlerStopsReceiving(RDCHelloTests* suite) {
    RecordingConnectState state(&suite->rdc, /*stateId=*/1);
    mountState(&state, &suite->device);
    connectOutJack(suite, ChainRole::RING, /*userId=*/1234);
    ASSERT_EQ(state.events.size(), 1u);

    state.setOnJackState(nullptr);
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

    std::vector<uint8_t> ctx = pdnContextBytes(ChainRole::RING, /*userId=*/1234, /*seqId=*/9);
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

// A state destroyed while still mounted — app teardown, which never dismounts —
// must not leave its `this`-bound lambda behind in the device-owned coordinator.
inline void connectStateDestructorReleasesSlot(RDCHelloTests* suite) {
    // Heap-allocated so ASan can prove the dispatch below would hit freed memory.
    // Only `-e native_asan` enforces that; under `-e native` this proves nothing.
    RecordingConnectState* state = new RecordingConnectState(&suite->rdc, /*stateId=*/1);
    mountState(state, &suite->device);
    delete state;

    connectOutJack(suite, ChainRole::CHILD, /*userId=*/4242);
    SUCCEED();
}

// Two subscribers on one coordinator.
inline void connectStateTwoMountedStatesKeepTheirOwnSubscription(RDCHelloTests* suite) {
    RecordingConnectState first(&suite->rdc, /*stateId=*/1);
    RecordingConnectState second(&suite->rdc, /*stateId=*/2);
    mountState(&first, &suite->device);
    mountState(&second, &suite->device);

    connectOutJack(suite, ChainRole::CHILD, /*userId=*/4242);
    EXPECT_EQ(first.events.size(), 1u) << "the second mount displaced the first";
    EXPECT_EQ(second.events.size(), 1u);
    EXPECT_EQ(suite->connectCount, 1) << "the fixture's own observer was displaced";

    dismountState(&second, &suite->device);
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(first.events.size(), 2u) << "one state's dismount unsubscribed the other";
    EXPECT_FALSE(first.events[1].connected);
    EXPECT_EQ(second.events.size(), 1u);
    dismountState(&first, &suite->device);
}
