#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"

class ConnectState : public State {
public:
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId, bool primaryRequired = true, bool auxRequired = false) : State(stateId), primaryRequired(primaryRequired), auxRequired(auxRequired) {
        this->remoteDeviceCoordinator = remoteDeviceCoordinator;
    };

    virtual ~ConnectState() {
        remoteDeviceCoordinator = nullptr;
    }

    bool isConnected() {
        return (primaryRequired && remoteDeviceCoordinator->getPortStatus(SerialByState::PRIMARY) == PortStatus::CONNECTED) &&
               (auxRequired && remoteDeviceCoordinator->getPortStatus(SerialByState::AUXILIARY) == PortStatus::CONNECTED);
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

private:
    
    const bool primaryRequired;
    const bool auxRequired;
};