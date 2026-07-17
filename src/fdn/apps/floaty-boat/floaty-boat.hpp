#pragma once

#include "state/typed-state-machine.hpp"
#include "state/fdn-connect-state.hpp"
#include "device/fdn.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "apps/fdn-app-ids.hpp"
#include <string>

class FloatyBoatDisconnectPolicy : public FDNConnectState {
public:
    explicit FloatyBoatDisconnectPolicy(RemoteDeviceCoordinator* remoteDeviceCoordinator)
        : FDNConnectState(remoteDeviceCoordinator, -1) {}
};

class FloatyBoat : public TypedStateMachine<FDN> {
public:
    FloatyBoat(int stateId,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               ControllerWirelessManager* controllerWirelessManager);
    ~FloatyBoat();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;

    int primaryScore = 0;
    std::string primaryScoreLabel = "SCORE";

private:
    static constexpr int kMainMenuStateIndex = 0;
    FloatyBoatDisconnectPolicy disconnectPolicy;
    ControllerWirelessManager* controllerWirelessManager;
};
