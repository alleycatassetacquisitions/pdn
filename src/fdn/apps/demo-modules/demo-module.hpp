#pragma once

#include "state/typed-state-machine.hpp"
#include "state/fdn-connect-state.hpp"
#include "device/fdn.hpp"
#include "device/remote-device-coordinator.hpp"
#include "apps/fdn-app-ids.hpp"

class DemoModuleDisconnectPolicy : public FDNConnectState {
public:
    explicit DemoModuleDisconnectPolicy(RemoteDeviceCoordinator* remoteDeviceCoordinator)
        : FDNConnectState(remoteDeviceCoordinator, -1) {}
};

class DemoModule : public TypedStateMachine<FDN> {
public:
    DemoModule(int stateId, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~DemoModule();

    void populateStateMap() override;

private:
    DemoModuleDisconnectPolicy disconnectPolicy;
};
