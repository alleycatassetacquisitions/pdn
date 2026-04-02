#pragma once

#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "device/device.hpp"
#include "apps/fdn-connect/fdn-connect-resources.hpp"

enum FDNConnectStateId {
    INITIAL_STATE = 100,
};

class FDNConnectInitialState : public ConnectState {
public:
    FDNConnectInitialState(RemoteDeviceCoordinator* remoteDeviceCoordinator);

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:

};