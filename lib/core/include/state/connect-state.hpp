#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/debounced-condition.hpp"

#include <array>
#include <cstring>
#include <functional>
#include <optional>

/// The peer's self-description on one jack, copied out of coordinator storage so
/// it stays valid past the next context on that jack and past link death. Opaque
/// here: `peerType` says which struct `profile` holds, and the game layer decodes
/// it.
struct ConnectionContext {
    DeviceType peerType = DeviceType::UNKNOWN;
    uint8_t chainRole = 0;
    std::array<uint8_t, RemoteDeviceCoordinator::MAX_PEER_PROFILE_BYTES> profile{};
    size_t profileLen = 0;
};

/// A jack's connection plus, on a connect, the peer's context. Absent on a
/// disconnect, and absent on a connect the exchange has not stored a profile for
/// — which the link SM makes unreachable today, its one transition into CONNECTED
/// being gated on that exchange completing.
struct JackConnectionState {
    bool connected = false;
    std::optional<ConnectionContext> context;
};

/*
 * The coordinator's observer slot is single, and the mounted ConnectState holds it.
 * That works because StateMachine dismounts the outgoing state before mounting the
 * incoming one, handing the slot over cleanly; a second ConnectState mounted without
 * that dismount would take the slot and leave the first one permanently deaf.
 *
 * `subscribed` records that this state registered, not that it still holds the slot
 * — the coordinator arbitrates nothing — so the two clear paths below are only safe
 * under that same dismount-before-mount ordering.
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
        // `this`-bound lambda in the device-owned coordinator.
        if (subscribed) unsubscribe();
        remoteDeviceCoordinator = nullptr;
    }

    using JackChangeHandler =
        std::function<void(SerialIdentifier jack, const JackConnectionState& state)>;

    /// Registers the handler for every jack edge during this state's tenure: a
    /// connect or disconnect on a jack this device owns, plus a replay of each
    /// already-connected jack at mount. Both come from the HELLO link machine, so
    /// neither fires while HELLO is off — which is every shipping build today.
    ///
    /// Register once, from the constructor or from onStateMounted — the mount replay
    /// runs after both, and the slot is not cleared on dismount, so one registration
    /// covers every tenure. A handler capturing anything shorter-lived than the state
    /// must replace itself before that thing dies.
    ///
    /// Handlers must tolerate re-receiving an event they already acted on, and only
    /// *connected* jacks are replayed — a disconnect between tenures is never
    /// delivered, so reset per-jack state in onStateDismounted rather than waiting
    /// for a clear. On a disconnect the polled surface has not caught up yet:
    /// getPortStatus and isConnected() still read CONNECTED for that jack.
    ///
    /// The context is read at dispatch time rather than captured when it arrived,
    /// so a state mounted long after the peer connected still gets it. getPeerMac
    /// stays unusable from here: it reads the handshake peer table, which HELLO
    /// quiesces (#159 re-sources it onto the link).
    void setOnJackChange(JackChangeHandler handler) {
        jackChangeHandler = std::move(handler);
    }

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
    // ConnectState, and nothing else would clear the slot.
    void beforeDismount(DeviceT* device) final {
        if (subscribed) unsubscribe();
    }

    void subscribe() {
        remoteDeviceCoordinator->setOnJackChange(
            [this](SerialIdentifier jack, bool connected) {
                dispatchJackChange(jack, connected);
            });
        subscribed = true;
    }

    void dispatchJackChange(SerialIdentifier jack, bool connected) {
        if (jackChangeHandler) jackChangeHandler(jack, buildJackState(jack, connected));
    }

    // On a disconnect the link is mid-teardown, so the peer facts still describe
    // whoever just left; handing them over as a live context would invite acting
    // on a departed peer.
    JackConnectionState buildJackState(SerialIdentifier jack, bool connected) const {
        JackConnectionState state;
        state.connected = connected;
        if (!connected) return state;

        size_t length = 0;
        const uint8_t* profile = remoteDeviceCoordinator->getPeerProfile(jack, length);
        if (profile == nullptr) return state;

        ConnectionContext context;
        context.peerType = remoteDeviceCoordinator->getPeerDeviceType(jack);
        context.chainRole = remoteDeviceCoordinator->getPeerChainRole(jack);
        context.profileLen = length;
        memcpy(context.profile.data(), profile, length);
        state.context = context;
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
        for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
            if (remoteDeviceCoordinator->getHelloLinkState(jack) ==
                RemoteDeviceCoordinator::HelloLinkState::CONNECTED) {
                dispatchJackChange(jack, true);
            }
        }
    }

    /// True when the given jack's port status is CONNECTED.
    bool isJackConnected(SerialIdentifier jack) const {
        return remoteDeviceCoordinator->getPortStatus(jack) == PortStatus::CONNECTED;
    }

    static constexpr unsigned long DISCONNECT_DEBOUNCE_MS = 500;
    DebouncedCondition disconnectDebounce;
    JackChangeHandler jackChangeHandler;
    bool subscribed = false;
};
