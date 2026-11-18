#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/debounced-condition.hpp"

#include <array>
#include <cstring>
#include <functional>
#include <optional>

/**
 * The peer's self-description on one jack, copied out of coordinator storage so
 * it stays valid past the next context on that jack and past link death. Opaque
 * here: `peerType` says which struct `profile` holds, and the game layer decodes
 * it.
 */
struct ConnectionContext {
    DeviceType peerType = DeviceType::UNKNOWN;
    uint8_t chainRole = 0;
    std::array<uint8_t, RemoteDeviceCoordinator::MAX_PEER_PROFILE_BYTES> profile{};
    size_t profileLen = 0;
};

/**
 * A jack's connection plus, on a connect, the peer's context. Absent on a
 * disconnect, and absent on a connect the exchange has not stored a profile for
 * — which the link SM makes unreachable today, its one transition into CONNECTED
 * being gated on that exchange completing.
 */
struct JackConnectionState {
    bool connected = false;
    std::optional<ConnectionContext> context;
};

/*
 * The coordinator has one jack-observer slot; ConnectState relies on StateMachine
 * dismounting the outgoing state before mounting the next to hand it over.
 */
template <typename DeviceT>
class ConnectState : public TypedState<DeviceT> {
public:
    /** Binds the state to the coordinator whose jack statuses it watches. */
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : TypedState<DeviceT>(stateId)
        , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

    /** Clears the coordinator's jack observer slot if still subscribed; non-owning. */
    ~ConnectState() override {
        // App teardown deletes states without dismounting them, which would leave a
        // `this`-bound lambda in the device-owned coordinator.
        if (subscribed) unsubscribe();
        remoteDeviceCoordinator = nullptr;
    }

    using JackStateHandler =
        std::function<void(SerialIdentifier jack, const JackConnectionState& state)>;

    /**
     * Handler for every jack edge while mounted, plus a replay of connected jacks at
     * mount. Register once; handlers must tolerate repeats, a disconnect while
     * dismounted is never delivered, and on a disconnect getJackStatus still reads
     * CONNECTED.
     */
    void setOnJackState(JackStateHandler handler) {
        jackStateHandler = std::move(handler);
    }

    /** The direct peer's hardware kind on the given jack. */
    DeviceType getPeerDeviceType(SerialIdentifier jack) const {
        return remoteDeviceCoordinator->getPeerDeviceType(jack);
    }

    /**
     * True when ANY jack this state requires reports CONNECTED. A state that
     * requires two jacks is "connected" on either one, not both.
     */
    bool isConnected() {
        return (isPrimaryRequired() && isJackConnected(SerialIdentifier::OUTPUT_JACK)) ||
               (isAuxRequired() && isJackConnected(SerialIdentifier::INPUT_JACK)) ||
               (isSecondaryRequired() && isJackConnected(SerialIdentifier::INPUT_JACK_SECONDARY));
    }

    /** isConnected() has been false for the full debounce window. */
    bool isPersistentlyDisconnected() {
        return disconnectDebounce.heldFor(!isConnected(), DISCONNECT_DEBOUNCE_MS);
    }

    /**
     * Restart the disconnect window: the next isPersistentlyDisconnected()
     * requires a fresh full debounce regardless of any run already in flight.
     */
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
                dispatchJackState(jack, connected);
            });
        subscribed = true;
    }

    void dispatchJackState(SerialIdentifier jack, bool connected) {
        if (jackStateHandler) jackStateHandler(jack, buildJackState(jack, connected));
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
        // Ask the link machine directly: it is the only source of jack edges, so
        // replaying anything else would deliver a connect whose matching
        // disconnect can never arrive.
        for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
            if (remoteDeviceCoordinator->getHelloLinkState(jack) ==
                RemoteDeviceCoordinator::HelloLinkState::CONNECTED) {
                dispatchJackState(jack, true);
            }
        }
    }

    bool isJackConnected(SerialIdentifier jack) const {
        return remoteDeviceCoordinator->getJackStatus(jack) == JackStatus::CONNECTED;
    }

    static constexpr unsigned long DISCONNECT_DEBOUNCE_MS = 500;
    DebouncedCondition disconnectDebounce;
    JackStateHandler jackStateHandler;
    bool subscribed = false;
};
