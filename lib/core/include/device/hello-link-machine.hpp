#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#include "state/state-machine.hpp"
#include "device/serial-manager.hpp"
#include "device/serial-frame-parser.hpp"

// Per-jack HELLO link state machine (#155). One instance per jack, driven by the
// RDC. Three states — Idle / Connecting / Connected — on the project's StateMachine
// framework.
//
// Liveness (lastHelloMs) is inherently cross-state: every HELLO refreshes the
// watchdog in Connecting and Connected alike, so it lives on this shared context,
// not on any one State. The per-transition triggers stay where the framework wants
// them — private flags on the state that owns the edge, set by the machine's event
// inputs and read by the transition predicates.

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
    std::function<void()> resetParser;                         // on link death
    // Fired once per link death with the peer the link was tracking; both the
    // Connected silent-link edge and the Connecting timeout converge on Idle.
    std::function<void(SerialIdentifier, const std::array<uint8_t, 6>&)> onLinkDown;

    unsigned long silentLinkMs = 100;
    unsigned long contextTimeoutMs = 500;

    unsigned long lastHelloMs = 0;
    std::array<uint8_t, 6> peerMac{};  // source MAC last heard; consumed by #157

    // Silent-link gap with the unsigned-underflow clamp: the UART task can stamp
    // lastHelloMs just after now is read, so now - lastHelloMs would wrap to ~UINT_MAX.
    unsigned long sinceHello() const {
        const unsigned long now = nowMs();
        return now >= lastHelloMs ? now - lastHelloMs : 0;
    }
};

class HelloIdleState : public State {
public:
    explicit HelloIdleState(HelloLinkContext* context)
        : State(HELLO_LINK_IDLE)
        , context(context) {}

    // Every teardown (silent link or context timeout) converges on Idle, so this is
    // the one seam where a tracked peer is released. The initial mount sees an
    // all-zero peerMac and fires nothing; clearing keeps peer() true to its contract.
    void onStateMounted(Device*) override {
        transitionToConnectingState = false;
        const std::array<uint8_t, 6> zero{};
        if (context->peerMac != zero) {
            if (context->onLinkDown) context->onLinkDown(context->jack, context->peerMac);
            context->peerMac = zero;
        }
    }

    void arm() { transitionToConnectingState = true; }
    bool transitionToConnecting() { return transitionToConnectingState; }

private:
    HelloLinkContext* context = nullptr;
    bool transitionToConnectingState = false;
};

class HelloConnectingState : public State {
public:
    explicit HelloConnectingState(HelloLinkContext* context)
        : State(HELLO_LINK_CONNECTING), context(context) {}

    void onStateMounted(Device*) override {
        connectingSinceMs = context->nowMs();
        transitionToConnectedState = false;
        // Every jack initiates its own context on connecting and completes when the
        // peer's arrives; there is no initiator/replier asymmetry, so two devices
        // whose jacks face each other (a 2-node ring) can't ping-pong.
        if (context->onContextRequest) context->onContextRequest(context->jack);
    }

    void markContextComplete() { transitionToConnectedState = true; }
    bool transitionToConnected() { return transitionToConnectedState; }

    // No HELLO within the silent-link window, or a context exchange that never
    // completes, drops the link back to Idle.
    bool transitionToIdle() {
        const unsigned long now = context->nowMs();
        const unsigned long connecting =
            now >= connectingSinceMs ? now - connectingSinceMs : 0;
        return context->sinceHello() > context->silentLinkMs ||
               connecting > context->contextTimeoutMs;
    }

private:
    HelloLinkContext* context = nullptr;
    unsigned long connectingSinceMs = 0;
    bool transitionToConnectedState = false;
};

class HelloConnectedState : public State {
public:
    explicit HelloConnectedState(HelloLinkContext* context)
        : State(HELLO_LINK_CONNECTED), context(context) {}

    void onStateMounted(Device*) override {
        if (context->onJackChange) context->onJackChange(context->jack, true);
    }

    // Leaving Connected means the link died: fire the disconnect and reset the
    // parser so a half-read frame from the dying peer cannot merge into the next
    // device that plugs in.
    void onStateDismounted(Device*) override {
        if (context->onJackChange) context->onJackChange(context->jack, false);
        if (context->resetParser) context->resetParser();
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
        context.lastHelloMs = context.nowMs();
        memcpy(context.peerMac.data(), hello.source, 6);
        if (currentState && currentState->getStateId() == HELLO_LINK_IDLE) {
            static_cast<HelloIdleState*>(currentState)->arm();
        }
    }

    // #157 drives this when the context exchange completes; only meaningful while
    // Connecting (mirrors the enum's state guard).
    void onContextExchangeComplete() {
        if (currentState && currentState->getStateId() == HELLO_LINK_CONNECTING) {
            static_cast<HelloConnectingState*>(currentState)->markContextComplete();
        }
    }

    int currentStateId() const {
        return currentState ? currentState->getStateId() : HELLO_LINK_IDLE;
    }

    /// The peer this link tracks (last HELLO source); all-zero while Idle.
    const std::array<uint8_t, 6>& peer() const { return context.peerMac; }

private:
    HelloLinkContext context;
};
