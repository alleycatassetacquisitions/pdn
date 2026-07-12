#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "rdc-hello-tests.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"

#include <cstring>
#include <vector>

// ============================================
// ConnectState per-jack connection callbacks (#165)
// ============================================
//
// End-to-end dispatch: HELLO/context events raised by the real RDC (driven via
// the base fixture's jacks + transport) flow through Quickdraw's dispatcher
// into whichever ConnectState is current.

// Routes Quickdraw at the base fixture's HELLO-driven RDC instead of
// MockDevice's built-in fake, so the dispatcher under test sees real RDC
// callbacks.
class HelloDispatchDevice : public MockDevice {
public:
    RemoteDeviceCoordinator* helloRdc = nullptr;
    /// The HELLO-driven RDC the base fixture owns.
    RemoteDeviceCoordinator* getRemoteDeviceCoordinator() override { return helloRdc; }
};

// An FdnConnectionContext serialized to its wire bytes for deliverIncoming.
inline std::vector<uint8_t> fdnContextBytes(uint8_t chainRole, uint8_t seqId) {
    FdnConnectionContext ctx{};
    ctx.seqId = seqId;
    ctx.chainRole = chainRole;
    std::vector<uint8_t> bytes(sizeof(ctx));
    memcpy(bytes.data(), &ctx, sizeof(ctx));
    return bytes;
}

class ConnectStateCallbackTests : public RDCHelloTests {
public:
    /// Builds a Quickdraw against the base fixture's RDC; states are created
    /// but none is mounted yet.
    void SetUp() override {
        RDCHelloTests::SetUp();

        gameDevice.helloRdc = &rdc;
        ON_CALL(*gameDevice.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        MockDisplay* display = gameDevice.mockDisplay;
        ON_CALL(*display, invalidateScreen()).WillByDefault(Return(display));
        ON_CALL(*display, drawImage(_)).WillByDefault(Return(display));
        ON_CALL(*display, drawText(_, _, _)).WillByDefault(Return(display));
        ON_CALL(*display, setGlyphMode(_)).WillByDefault(Return(display));
        EXPECT_CALL(*device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
        EXPECT_CALL(*device.mockPeerComms, removeEspNowPeer(_)).Times(testing::AnyNumber());

        player = new Player();
        char playerId[] = "jack";
        player->setUserID(playerId);

        qwm = new FakeQuickdrawWirelessManager();
        // Constructing Quickdraw installs its RDC dispatch callbacks (the
        // wiring under test), replacing the base fixture's counters.
        quickdraw = new Quickdraw(player, &gameDevice, qwm, nullptr, nullptr);
        quickdraw->populateStateMap();
    }

    /// Tears down the game layer before the base fixture restores the clock.
    void TearDown() override {
        delete quickdraw;
        delete qwm;
        delete player;
        RDCHelloTests::TearDown();
    }

    /// Mounts the stateMap entry with the given state id.
    void mountState(int stateId) {
        ASSERT_TRUE(quickdraw->skipToStateById(&gameDevice, stateId));
        ASSERT_NE(quickdraw->getCurrentState(), nullptr);
        ASSERT_EQ(quickdraw->getCurrentState()->getStateId(), stateId);
    }

    /// Friend-gated poke: marks the mounted Idle as mid-match-initiation.
    void setIdleMatchInitialized(Idle* idle) {
        idle->matchInitialized = true;
    }

    /// Replaces the current ConnectState's observer with a recording spy.
    void installSpyOnCurrentState() {
        auto* connectState = static_cast<ConnectState<PDN>*>(quickdraw->getCurrentState());
        connectState->setOnJackChange(
            [this](SerialIdentifier jack, const JackConnectionState& state) {
                eventCount++;
                lastJack = jack;
                lastState = state;
            });
    }

    /// Drives the given jack to CONNECTED via a received peer context whose
    /// sender MAC leads with macFirstByte (helloFrame's fixed tail).
    void connectJackWithContext(NativeSerialDriver& jackDriver, uint8_t macFirstByte,
                                const std::vector<uint8_t>& contextBytes, PktType type) {
        deliverHello(jackDriver, helloFrame(macFirstByte));
        rdc.sync(&device);  // Idle -> Connecting
        const uint8_t mac[6] = {macFirstByte, 0x02, 0x03, 0x04, 0x05, 0x06};
        transport()->deliverIncoming(type, mac, contextBytes.data(), contextBytes.size());
        rdc.sync(&device);  // commit Connecting -> Connected; fires jack change
    }

    /// Drives the OUT jack to CONNECTED via a received peer context.
    void connectOutJackWithContext(const std::vector<uint8_t>& contextBytes, PktType type) {
        connectJackWithContext(outJack, 0xA1, contextBytes, type);
    }

    /// Drives the OUT jack to CONNECTED with no peer context arriving.
    void connectOutJackWithoutContext() {
        deliverHello(outJack, helloFrame(0xA1));
        rdc.sync(&device);
        rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
        rdc.sync(&device);
    }

    /// Friend-gated: true while any of the four RDC callback slots Quickdraw's
    /// constructor fills is still occupied.
    bool rdcHasQuickdrawCallbacks() const {
        return static_cast<bool>(rdc.contextReceivedCallback) ||
               static_cast<bool>(rdc.jackChangeCallback) ||
               static_cast<bool>(rdc.chainChangeCallback) ||
               static_cast<bool>(rdc.peerLostCallback);
    }

    /// Silent-links every live jack back to IDLE, firing disconnects.
    void dropAllJacks() {
        fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
        rdc.sync(&device);
    }

    HelloDispatchDevice gameDevice;
    Player* player = nullptr;
    FakeQuickdrawWirelessManager* qwm = nullptr;
    Quickdraw* quickdraw = nullptr;

    int eventCount = 0;
    SerialIdentifier lastJack = SerialIdentifier::OUTPUT_JACK;
    JackConnectionState lastState;
};

// A connect whose peer context arrived delivers connected=true plus the full
// ConnectionContext: peer kind, chainRole, and the raw profile bytes the game
// layer decodes itself.
inline void connectStateConnectEventCarriesContext(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    suite->installSpyOnCurrentState();

    suite->connectOutJackWithContext(
        pdnContextBytes(/*chainRole=*/2, /*userId=*/4242, /*seqId=*/9),
        PktType::kPdnConnectionContext);

    ASSERT_EQ(suite->eventCount, 1);
    EXPECT_EQ(suite->lastJack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_TRUE(suite->lastState.connected);
    ASSERT_TRUE(suite->lastState.context.has_value());
    EXPECT_EQ(suite->lastState.context->peerType, DeviceType::PDN);
    EXPECT_EQ(suite->lastState.context->chainRole, 2);
    ASSERT_EQ(suite->lastState.context->profileLen, sizeof(PlayerProfile));
    PlayerProfile profile{};
    memcpy(&profile, suite->lastState.context->profile.data(), sizeof(profile));
    EXPECT_EQ(profile.userId, 4242);
}

// A disconnect delivers connected=false and never a context, even though one
// arrived for the connection that just died.
inline void connectStateDisconnectEventHasNoContext(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    suite->installSpyOnCurrentState();
    suite->connectOutJackWithContext(
        pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3),
        PktType::kPdnConnectionContext);
    ASSERT_EQ(suite->eventCount, 1);

    suite->dropAllJacks();

    ASSERT_EQ(suite->eventCount, 2);
    EXPECT_EQ(suite->lastJack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_FALSE(suite->lastState.connected);
    EXPECT_FALSE(suite->lastState.context.has_value());
}

// Events go only to the CURRENT state: a jack event while a non-ConnectState
// is mounted is dropped (and must not crash), and the same registered
// observer receives events again once its state is remounted.
inline void connectStateEventsReachOnlyMountedState(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    suite->installSpyOnCurrentState();
    suite->mountState(SLEEP);

    suite->connectOutJackWithoutContext();
    EXPECT_EQ(suite->eventCount, 0);

    suite->mountState(IDLE);
    suite->dropAllJacks();
    ASSERT_EQ(suite->eventCount, 1);
    EXPECT_FALSE(suite->lastState.connected);
}

// The cached context dies with its connection: a reconnect whose own context
// has not (yet) arrived delivers nullopt, not the previous peer's context.
inline void connectStateReconnectWithoutContextDeliversNullopt(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    suite->installSpyOnCurrentState();
    suite->connectOutJackWithContext(
        pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3),
        PktType::kPdnConnectionContext);
    ASSERT_EQ(suite->eventCount, 1);
    ASSERT_TRUE(suite->lastState.context.has_value());

    suite->dropAllJacks();
    ASSERT_EQ(suite->eventCount, 2);

    // Past the RDC's context-buffer TTL (= the silent-link window), so nothing
    // stale can re-apply on reconnect.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->connectOutJackWithoutContext();

    ASSERT_EQ(suite->eventCount, 3);
    EXPECT_TRUE(suite->lastState.connected);
    EXPECT_FALSE(suite->lastState.context.has_value());
}

// An FDN peer context arriving with the connect event arms Idle's symbol
// transition without getPeerDeviceType polling (the quiesced handshake
// reports UNKNOWN under HELLO connectivity). A registered external observer
// coexists with the arming logic: taking the callback slot must not sever it.
inline void connectStateIdleArmsSymbolFromFdnContext(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    Idle* idle = static_cast<Idle*>(suite->quickdraw->getCurrentState());
    suite->installSpyOnCurrentState();
    ASSERT_FALSE(idle->transitionToSymbol());

    suite->connectOutJackWithContext(fdnContextBytes(/*chainRole=*/0, /*seqId=*/5),
                                     PktType::kFdnConnectionContext);

    EXPECT_TRUE(idle->transitionToSymbol());
    EXPECT_EQ(suite->eventCount, 1);
}

// The secondary input jack never arms symbol match: Idle's onJackEvent
// ignores it, so an FDN there leaves the transition unarmed.
inline void connectStateIdleIgnoresFdnOnSecondaryJack(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    Idle* idle = static_cast<Idle*>(suite->quickdraw->getCurrentState());

    suite->connectJackWithContext(suite->secondaryJack, 0xC1,
                                  fdnContextBytes(/*chainRole=*/0, /*seqId=*/5),
                                  PktType::kFdnConnectionContext);

    EXPECT_FALSE(idle->transitionToSymbol());
}

// A duel already being initiated wins over a late FDN plug-in: with
// matchInitialized set, an FDN connect on OUTPUT must not arm symbol match.
inline void connectStateIdleFdnIgnoredWhileMatchInitialized(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    Idle* idle = static_cast<Idle*>(suite->quickdraw->getCurrentState());
    suite->setIdleMatchInitialized(idle);

    suite->connectOutJackWithContext(fdnContextBytes(/*chainRole=*/0, /*seqId=*/5),
                                     PktType::kFdnConnectionContext);

    EXPECT_FALSE(idle->transitionToSymbol());
}

// A state mounted after the connect edge still learns the snapshot: on the
// loop tick after a mount, Quickdraw replays each connected jack's state.
inline void connectStateMountReplayDeliversSnapshot(ConnectStateCallbackTests* suite) {
    suite->mountState(SLEEP);
    suite->connectOutJackWithContext(fdnContextBytes(/*chainRole=*/0, /*seqId=*/5),
                                     PktType::kFdnConnectionContext);

    suite->mountState(IDLE);
    Idle* idle = static_cast<Idle*>(suite->quickdraw->getCurrentState());
    ASSERT_FALSE(idle->transitionToSymbol());

    suite->quickdraw->onStateLoop(&suite->gameDevice);

    EXPECT_TRUE(idle->transitionToSymbol());
}

// ~Quickdraw must clear its this-capturing RDC callbacks: the coordinator
// outlives the app, so a jack event after destruction would otherwise call
// into freed memory. Asserted on the RDC's slots directly — the use-after-free
// is UB that a plain native build rarely turns into a visible crash.
inline void connectStateQuickdrawDtorClearsRdcCallbacks(ConnectStateCallbackTests* suite) {
    ASSERT_TRUE(suite->rdcHasQuickdrawCallbacks());

    delete suite->quickdraw;
    suite->quickdraw = nullptr;

    EXPECT_FALSE(suite->rdcHasQuickdrawCallbacks());
}

// Replay is once-per-mount, not once-per-tick: a loop with no state change
// must not re-deliver the jack snapshot.
inline void connectStateReplayFiresOncePerMount(ConnectStateCallbackTests* suite) {
    suite->mountState(IDLE);
    suite->installSpyOnCurrentState();
    suite->connectOutJackWithoutContext();
    ASSERT_EQ(suite->eventCount, 1);

    suite->quickdraw->onStateLoop(&suite->gameDevice);
    // One connect edge + one mount replay, nothing else.
    ASSERT_EQ(suite->eventCount, 2);
    suite->quickdraw->onStateLoop(&suite->gameDevice);

    EXPECT_EQ(suite->eventCount, 2);
}
