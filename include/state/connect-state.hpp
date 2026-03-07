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

    bool isConnected() {
        return (isPrimaryRequired() && remoteDeviceCoordinator->getPortStatus(SerialIdentifier::OUTPUT_JACK) == PortStatus::CONNECTED) ||
               (isAuxRequired() && remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED);
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    virtual bool isPrimaryRequired() = 0;
    virtual bool isAuxRequired() = 0;

private:
    
};