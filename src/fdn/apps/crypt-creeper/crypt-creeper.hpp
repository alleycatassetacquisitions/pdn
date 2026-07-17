#pragma once

#include "state/typed-state-machine.hpp"
#include "state/fdn-connect-state.hpp"
#include "device/fdn.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "apps/fdn-app-ids.hpp"

class CryptCreeperDisconnectPolicy : public FDNConnectState {
public:
    explicit CryptCreeperDisconnectPolicy(RemoteDeviceCoordinator* remoteDeviceCoordinator)
        : FDNConnectState(remoteDeviceCoordinator, -1) {}
};

class CryptCreeper : public TypedStateMachine<FDN> {
public:
    CryptCreeper(int stateId,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               ControllerWirelessManager* controllerWirelessManager);
    ~CryptCreeper();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;

private:
    static constexpr int kMainMenuStateIndex = 0;
    CryptCreeperDisconnectPolicy disconnectPolicy;
    ControllerWirelessManager* controllerWirelessManager;
    unsigned long elapsedMs_ = 0;
};
