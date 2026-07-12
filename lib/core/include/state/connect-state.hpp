#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/debounced-condition.hpp"

template<typename DeviceT>
class ConnectState : public TypedState<DeviceT> {
public:
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
};
