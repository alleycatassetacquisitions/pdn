#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/debounced-condition.hpp"

class ConnectState : public State {
public:
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId) : State(stateId) {
        this->remoteDeviceCoordinator = remoteDeviceCoordinator;
    };

    virtual ~ConnectState() {
        remoteDeviceCoordinator = nullptr;
    }

    const DeviceType getPeerDeviceType(SerialIdentifier port) const {
        return remoteDeviceCoordinator->getPeerDeviceType(port);
    }

    bool isConnected() {
        auto connectedOrChain = [this](SerialIdentifier jack) {
            PortStatus s = remoteDeviceCoordinator->getPortStatus(jack);
            return s == PortStatus::CONNECTED || s == PortStatus::DAISY_CHAINED;
        };
        return (isPrimaryRequired() && connectedOrChain(SerialIdentifier::OUTPUT_JACK)) ||
               (isAuxRequired() && connectedOrChain(SerialIdentifier::INPUT_JACK));
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
