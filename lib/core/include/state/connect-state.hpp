#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/debounced-condition.hpp"

template<typename DeviceT>
class ConnectState : public TypedState<DeviceT> {
public:
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : TypedState<DeviceT>(stateId), remoteDeviceCoordinator(remoteDeviceCoordinator) {}

    virtual ~ConnectState() {
        remoteDeviceCoordinator = nullptr;
    }

    const DeviceType getPeerDeviceType(SerialIdentifier port) const {
        return remoteDeviceCoordinator->getPeerDeviceType(port);
    }

    bool isConnected() {
        auto connected = [this](SerialIdentifier jack) {
            return remoteDeviceCoordinator->getPortStatus(jack) == PortStatus::CONNECTED;
        };
        return (isPrimaryRequired() && connected(SerialIdentifier::OUTPUT_JACK)) ||
               (isAuxRequired() && connected(SerialIdentifier::INPUT_JACK));
    }

    bool isPersistentlyDisconnected() {
        return disconnectDebounce_.heldFor(!isConnected(), kDisconnectDebounceMs);
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    virtual bool isPrimaryRequired() = 0;
    virtual bool isAuxRequired() = 0;

private:
    static constexpr unsigned long kDisconnectDebounceMs = 500;
    DebouncedCondition disconnectDebounce_;
};
