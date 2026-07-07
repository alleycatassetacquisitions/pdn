#pragma once

#include "state/typed-state-machine.hpp"
#include "state/fdn-connect-state.hpp"
#include "device/fdn.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "apps/fdn-app-ids.hpp"

class BonkItDisconnectPolicy : public FDNConnectState {
public:
    explicit BonkItDisconnectPolicy(RemoteDeviceCoordinator* remoteDeviceCoordinator)
        : FDNConnectState(remoteDeviceCoordinator, -1) {}
};

class BonkIt : public TypedStateMachine<FDN> {
public:
    BonkIt(int stateId,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               ControllerWirelessManager* controllerWirelessManager);
    ~BonkIt();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;

private:
    static constexpr int kMainMenuStateIndex = 0;
    BonkItDisconnectPolicy disconnectPolicy;
    ControllerWirelessManager* controllerWirelessManager;
};
