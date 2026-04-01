#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"

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
        return (isJackRequired(SerialIdentifier::INPUT_JACK) && remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED) ||
               (isJackRequired(SerialIdentifier::INPUT_JACK_SECONDARY) && remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK_SECONDARY) == PortStatus::CONNECTED);
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    virtual bool isJackRequired(SerialIdentifier jack) = 0;
};