#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/debounced-condition.hpp"

#include <optional>

/// The peer's self-description on one jack. `profile` points into per-jack
/// coordinator storage, which the next context on that jack overwrites and link
/// death zeroes, so copy anything you keep rather than holding the pointer.
/// Opaque here: `peerType` says which struct `profile` holds, and the game layer
/// decodes it.
struct ConnectionContext {
    DeviceType peerType = DeviceType::UNKNOWN;
    uint8_t chainRole = 0;
    const uint8_t* profile = nullptr;
    size_t profileLen = 0;
};

/// A jack's connection plus, on a connect, the peer's context. Absent on a
/// disconnect. Optional rather than plain because nothing enforces that a jack
/// only reaches CONNECTED after its context exchange completes; today the only
/// path there runs through the exchange, so a connect always carries one.
struct JackConnectionState {
    bool connected = false;
    std::optional<ConnectionContext> context;
};

/*
 * The coordinator's observer slot is single. This relies on StateMachine
 * dismounting the outgoing state before mounting the incoming one, which hands
 * the slot over cleanly; a second ConnectState mounted without that dismount
 * would take the slot and leave the first one permanently deaf.
 */
template <typename DeviceT>
class ConnectState : public TypedState<DeviceT> {
public:
    /// Binds the state to the coordinator whose jack statuses it watches.
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : TypedState<DeviceT>(stateId)
        , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

    /// Non-owning; just drops the coordinator pointer.
    ~ConnectState() override {
        // App teardown deletes states without dismounting them, which would leave a
        // `this`-bound lambda in the device-owned coordinator. `subscribed` is false
        // after a normal dismount, so the handover path cannot clear a successor's
        // slot; a state destroyed while still mounted alongside another one can.
        if (subscribed) unsubscribe();
        remoteDeviceCoordinator = nullptr;
    }

    /// A jack this device owns connected or disconnected, plus a replay of each
    /// already-connected jack at mount. Both come from the HELLO link machine, so
    /// neither fires while HELLO is off — which is every shipping build today.
    ///
    /// Handlers must tolerate re-receiving an event they already acted on, and
    /// only *connected* jacks are replayed — a disconnect between tenures is never
    /// delivered, so reset per-jack state in onStateDismounted rather than waiting
    /// for a clear. On a disconnect the polled surface has not caught up yet:
    /// getPortStatus and isConnected() still read CONNECTED for that jack.
    ///
    /// The context is read at dispatch time rather than captured when it arrived,
    /// so a state mounted long after the peer connected still gets it. getPeerMac
    /// remains unusable from here: it reads the handshake peer table, which HELLO
    /// quiesces (#159 re-sources it onto the link).
    virtual void onJackChange(SerialIdentifier jack, const JackConnectionState& state) {}

    /// The direct peer's hardware kind on the given port.
    DeviceType getPeerDeviceType(SerialIdentifier port) const {
        return remoteDeviceCoordinator->getPeerDeviceType(port);
    }

    /// True when ANY jack this state requires reports CONNECTED. A state that
    /// requires two jacks is "connected" on either one, not both.
    bool isConnected() {
        return (isPrimaryRequired() && isJackConnected(SerialIdentifier::OUTPUT_JACK)) ||
               (isAuxRequired() && isJackConnected(SerialIdentifier::INPUT_JACK)) ||
               (isSecondaryRequired() && isJackConnected(SerialIdentifier::INPUT_JACK_SECONDARY));
    }

    /// isConnected() has been false for the full debounce window.
    bool isPersistentlyDisconnected() {
        return disconnectDebounce.heldFor(!isConnected(), DISCONNECT_DEBOUNCE_MS);
    }

    /// Restart the disconnect window: the next isPersistentlyDisconnected()
    /// requires a fresh full debounce regardless of any run already in flight.
    void resetDisconnectDebounce() {
        disconnectDebounce.reset();
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    virtual bool isPrimaryRequired() = 0;
    virtual bool isAuxRequired() = 0;
    // Override and return true to also consider INPUT_JACK_SECONDARY when
    // evaluating isConnected(). Devices with a single input jack leave this false.
    virtual bool isSecondaryRequired() { return false; }

private:
    // TypedState brackets the subclass hooks with these, so the replay lands after
    // the subclass's own mount hook rather than reaching members it has not set up.
    void afterMount(DeviceT* device) final {
        subscribe();
        replayConnectedJacks();
    }

    // A dismounted state must stop receiving; the next mounted state may not be a
    // ConnectState, and nothing else would clear the slot. Guarded on `subscribed`
    // for the same reason the destructor is: never clear a slot we do not hold.
    void beforeDismount(DeviceT* device) final {
        if (subscribed) unsubscribe();
    }

    // setChainChangeCallback and setPeerLostCallback are already claimed by the
    // app (Quickdraw), so a state must not take those.
    void subscribe() {
        remoteDeviceCoordinator->setOnJackChange(
            [this](SerialIdentifier jack, bool connected) {
                onJackChange(jack, buildJackState(jack, connected));
            });
        subscribed = true;
    }

    // On a disconnect the link is mid-teardown, so the peer facts still describe
    // whoever just left; handing them over as a live context would invite acting
    // on a departed peer.
    JackConnectionState buildJackState(SerialIdentifier jack, bool connected) const {
        JackConnectionState state;
        state.connected = connected;
        if (!connected) return state;

        ConnectionContext context;
        context.peerType = remoteDeviceCoordinator->getPeerDeviceType(jack);
        context.chainRole = remoteDeviceCoordinator->getPeerChainRole(jack);
        context.profile = remoteDeviceCoordinator->getPeerProfile(jack, context.profileLen);
        // No profile means the context exchange has not completed; the connect is
        // still real, so report it without one rather than withholding the event.
        if (context.profile != nullptr) state.context = context;
        return state;
    }

    void unsubscribe() {
        remoteDeviceCoordinator->setOnJackChange(nullptr);
        subscribed = false;
    }

    void replayConnectedJacks() {
        // Ask the link machine, not getPortStatus: the latter falls back to
        // handshake state on a jack HELLO does not own, and replaying from there
        // would deliver a connect whose matching disconnect can never arrive.
        // Per jack rather than per device, because whether a jack has a link
        // machine at all varies by hardware.
        for (SerialIdentifier jack : {SerialIdentifier::OUTPUT_JACK,
                                      SerialIdentifier::INPUT_JACK,
                                      SerialIdentifier::INPUT_JACK_SECONDARY}) {
            if (remoteDeviceCoordinator->getHelloLinkState(jack) ==
                RemoteDeviceCoordinator::HelloLinkState::CONNECTED) {
                onJackChange(jack, buildJackState(jack, true));
            }
        }
    }

    /// True when the given jack's port status is CONNECTED.
    bool isJackConnected(SerialIdentifier jack) const {
        return remoteDeviceCoordinator->getPortStatus(jack) == PortStatus::CONNECTED;
    }

    static constexpr unsigned long DISCONNECT_DEBOUNCE_MS = 500;
    DebouncedCondition disconnectDebounce;
    bool subscribed = false;
};
