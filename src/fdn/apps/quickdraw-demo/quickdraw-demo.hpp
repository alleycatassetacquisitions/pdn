#pragma once

#include "state/typed-state-machine.hpp"
#include "state/fdn-connect-state.hpp"
#include "device/fdn.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "apps/fdn-app-ids.hpp"
#include <string>

class QuickdrawDemoDisconnectPolicy : public FDNConnectState {
public:
    explicit QuickdrawDemoDisconnectPolicy(RemoteDeviceCoordinator* remoteDeviceCoordinator)
        : FDNConnectState(remoteDeviceCoordinator, -1) {}
};

class QuickdrawDemo : public TypedStateMachine<FDN> {
public:
    QuickdrawDemo(int stateId,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               ControllerWirelessManager* controllerWirelessManager);
    ~QuickdrawDemo();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;

    int primaryScore = 0;
    int secondaryScore = 0;
    std::string primaryScoreLabel = "WINS";
    std::string secondaryScoreLabel = "AVG MS";

private:
    static constexpr int kMainMenuStateIndex = 0;
    QuickdrawDemoDisconnectPolicy disconnectPolicy;
    ControllerWirelessManager* controllerWirelessManager;
};
