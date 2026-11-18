#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#include "state/state-machine.hpp"
#include "device/serial-manager.hpp"
#include "device/serial-frame-parser.hpp"

// Per-jack HELLO link machine. Liveness (lastHelloMs) is refreshed in every state,
// so it lives on the shared context, not on a State.

enum HelloLinkStateId {
    HELLO_LINK_MACHINE = 89,
    HELLO_LINK_IDLE = 90,
    HELLO_LINK_CONNECTING = 91,
    HELLO_LINK_CONNECTED = 92,
};

struct HelloLinkContext {
    SerialIdentifier jack{};
    std::function<unsigned long()> nowMs;
    std::function<void(SerialIdentifier)> onContextRequest;    // every jack, on entering Connecting
    std::function<void(SerialIdentifier, bool)> onJackChange;  // connect / disconnect
    // Fired on every Idle mount — the initial one included, with an all-zero
    // peerMac — carrying the peer the link was tracking; both the Connected
    // silent-link edge and the Connecting timeout converge here.
    std::function<void(SerialIdentifier, const std::array<uint8_t, 6>&)> onLinkDown;

    unsigned long silentLinkMs = 100;
    unsigned long contextTimeoutMs = 500;

    unsigned long lastHelloMs = 0;
    std::array<uint8_t, 6> peerMac{};  // source MAC last heard; the owner's per-jack peer identity

    /** Milliseconds elapsed since the last HELLO on this jack. */
    unsigned long sinceHello() const { return nowMs() - lastHelloMs; }
};

class HelloIdleState : public TypedState<Device> {
public:
    /** Binds the shared per-jack context; the machine owns the state. */
    explicit HelloIdleState(HelloLinkContext* context)
        : TypedState<Device>(HELLO_LINK_IDLE)
        , context(context) {}

    /**
     * Every link-death path mounts Idle (and the initial mount, with an all-zero
     * peerMac), so link-death cleanup hangs off onLinkDown here.
     */
    void onStateMounted(Device*) override {
        transitionToConnectingState = false;
        if (context->onLinkDown) context->onLinkDown(context->jack, context->peerMac);
        context->peerMac = {};
    }

    /**
     * Publishes the armed peer only on leaving Idle: peer() must read all-zero
     * while Idle, and Connecting's mount needs it.
     */
    void onStateDismounted(Device*) override { context->peerMac = armedPeer; }

    /** Opens the link on the next tick, with `source` as the peer it tracks. */
    void arm(const uint8_t* source) {
        memcpy(armedPeer.data(), source, 6);
        transitionToConnectingState = true;
    }
    bool transitionToConnecting() { return transitionToConnectingState; }

private:
    HelloLinkContext* context = nullptr;
    std::array<uint8_t, 6> armedPeer{};
    bool transitionToConnectingState = false;
};

class HelloConnectingState : public TypedState<Device> {
public:
    explicit HelloConnectingState(HelloLinkContext* context)
        : TypedState<Device>(HELLO_LINK_CONNECTING), context(context) {}

    void onStateMounted(Device*) override {
        connectingSinceMs = context->nowMs();
        transitionToConnectedState = false;
        // Every jack initiates its own context on connecting and completes when the
        // peer's arrives.
        if (context->onContextRequest) context->onContextRequest(context->jack);
    }

    void markContextComplete() { transitionToConnectedState = true; }
    bool transitionToConnected() { return transitionToConnectedState; }

    bool transitionToIdle() {
        return context->sinceHello() > context->silentLinkMs ||
               context->nowMs() - connectingSinceMs > context->contextTimeoutMs;
    }

private:
    HelloLinkContext* context = nullptr;
    unsigned long connectingSinceMs = 0;
    bool transitionToConnectedState = false;
};

class HelloConnectedState : public TypedState<Device> {
public:
    explicit HelloConnectedState(HelloLinkContext* context)
        : TypedState<Device>(HELLO_LINK_CONNECTED), context(context) {}

    void onStateMounted(Device*) override {
        if (context->onJackChange) context->onJackChange(context->jack, true);
    }

    // Leaving Connected means the link died: fire the disconnect. Teardown itself
    // (onLinkDown) lives in HelloIdleState — every link-death path enters Idle.
    void onStateDismounted(Device*) override {
        if (context->onJackChange) context->onJackChange(context->jack, false);
    }

    bool transitionToIdle() { return context->sinceHello() > context->silentLinkMs; }

private:
    HelloLinkContext* context = nullptr;
};

class HelloLinkMachine : public StateMachine {
public:
    explicit HelloLinkMachine(HelloLinkContext context)
        : StateMachine(HELLO_LINK_MACHINE), context(std::move(context)) {}

    void populateStateMap() override {
        auto* idle = new HelloIdleState(&context);
        auto* connecting = new HelloConnectingState(&context);
        auto* connected = new HelloConnectedState(&context);

        idle->addTransition(new StateTransition(
            [idle] { return idle->transitionToConnecting(); }, connecting));
        connecting->addTransition(new StateTransition(
            [connecting] { return connecting->transitionToConnected(); }, connected));
        connecting->addTransition(new StateTransition(
            [connecting] { return connecting->transitionToIdle(); }, idle));
        connected->addTransition(new StateTransition(
            [connected] { return connected->transitionToIdle(); }, idle));

        stateMap.push_back(idle);  // index 0 = initial state
        stateMap.push_back(connecting);
        stateMap.push_back(connected);
    }

    // A HELLO stamps liveness on every state; it only opens a new link while Idle.
    // The self-echo / all-zero reject happens upstream in the RDC (it owns selfMac).
    void onHelloReceived(const HelloPayload& hello) {
        // Different source on a live link = peer swap: drop to Idle (fires
        // onLinkDown) so this HELLO opens a fresh link.
        if (currentState && currentState->getStateId() != HELLO_LINK_IDLE &&
            memcmp(context.peerMac.data(), hello.source, 6) != 0) {
            skipToState(nullptr, 0);
        }
        context.lastHelloMs = context.nowMs();
        if (currentState && currentState->getStateId() == HELLO_LINK_IDLE) {
            static_cast<HelloIdleState*>(currentState)->arm(hello.source);
        }
    }

    // Driven when the context exchange completes; only meaningful while
    // Connecting.
    void onContextExchangeComplete() {
        if (currentState && currentState->getStateId() == HELLO_LINK_CONNECTING) {
            static_cast<HelloConnectingState*>(currentState)->markContextComplete();
        }
    }

    int currentStateId() const {
        return currentState ? currentState->getStateId() : HELLO_LINK_IDLE;
    }

    /**
     * True in the window between the exchange completing and the Connecting ->
     * Connected commit (which lands at the end of the next tick); the jack still
     * reports Connecting there, so callers use this to spot the duplicate.
     */
    bool didMarkContextComplete() const {
        return currentState && currentState->getStateId() == HELLO_LINK_CONNECTING &&
               static_cast<HelloConnectingState*>(currentState)->transitionToConnected();
    }

    /** The peer this link tracks (last HELLO source); all-zero while Idle. */
    const std::array<uint8_t, 6>& peer() const { return context.peerMac; }

private:
    HelloLinkContext context;
};
