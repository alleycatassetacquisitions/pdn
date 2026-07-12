#pragma once

#include "state/state.hpp"
#include "state/jack-connection-state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/debounced-condition.hpp"
#include <functional>

template<typename DeviceT>
class ConnectState : public TypedState<DeviceT> {
public:
    /// Per-jack connection event observer, invoked while this state is current.
    using JackChangeCallback = std::function<void(SerialIdentifier, const JackConnectionState&)>;

    /// Binds the state to the coordinator whose jack statuses it watches.
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : TypedState<DeviceT>(stateId), remoteDeviceCoordinator(remoteDeviceCoordinator) {}

    /// Non-owning; just drops the coordinator pointer.
    virtual ~ConnectState() {
        remoteDeviceCoordinator = nullptr;
    }

    /// The direct peer's hardware kind on the given port.
    const DeviceType getPeerDeviceType(SerialIdentifier port) const {
        return remoteDeviceCoordinator->getPeerDeviceType(port);
    }

    /// True when every jack this state requires reports CONNECTED.
    bool isConnected() {
        return (isPrimaryRequired() && isJackConnected(SerialIdentifier::OUTPUT_JACK)) ||
               (isAuxRequired() && isJackConnected(SerialIdentifier::INPUT_JACK)) ||
               (isSecondaryRequired() && isJackConnected(SerialIdentifier::INPUT_JACK_SECONDARY));
    }

    /// isConnected() has been false for the full debounce window.
    bool isPersistentlyDisconnected() {
        return disconnectDebounce_.heldFor(!isConnected(), kDisconnectDebounceMs);
    }

    /// Restart the disconnect window: the next isPersistentlyDisconnected()
    /// requires a fresh full debounce regardless of any run already in flight.
    void resetDisconnectDebounce() {
        disconnectDebounce_.reset();
    }

    /// Registers the observer onJackEvent() forwards to. One slot per state
    /// instance; registering again replaces the previous observer. Handlers
    /// may see the same connected snapshot again on remount (mount replay)
    /// and must be idempotent to a repeat. Replay arrives after the newly
    /// mounted state's first loop tick, and self-transitions do not re-replay.
    void setOnJackChange(JackChangeCallback callback) {
        jackChangeCallback = std::move(callback);
    }

    /// Dispatcher entry point: hands one jack event to the registered observer.
    void onJackEvent(SerialIdentifier jack, const JackConnectionState& state) override {
        if (jackChangeCallback) {
            jackChangeCallback(jack, state);
        }
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    virtual bool isPrimaryRequired() = 0;
    virtual bool isAuxRequired() = 0;
    // Override and return true to also consider INPUT_JACK_SECONDARY when
    // evaluating isConnected(). Devices with a single input jack leave this false.
    virtual bool isSecondaryRequired() { return false; }

private:
    /// True when the given jack's port status is CONNECTED.
    bool isJackConnected(SerialIdentifier jack) const {
        return remoteDeviceCoordinator->getPortStatus(jack) == PortStatus::CONNECTED;
    }

    static constexpr unsigned long kDisconnectDebounceMs = 500;
    DebouncedCondition disconnectDebounce_;
    JackChangeCallback jackChangeCallback;
};
