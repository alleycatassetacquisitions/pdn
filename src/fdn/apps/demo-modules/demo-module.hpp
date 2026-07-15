#pragma once

#include "state/typed-state-machine.hpp"
#include "state/fdn-connect-state.hpp"
#include "device/fdn.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "apps/fdn-app-ids.hpp"
#include <string>

class DemoModuleDisconnectPolicy : public FDNConnectState {
public:
    explicit DemoModuleDisconnectPolicy(RemoteDeviceCoordinator* remoteDeviceCoordinator)
        : FDNConnectState(remoteDeviceCoordinator, -1) {}
};

class DemoModule : public TypedStateMachine<FDN> {
public:
    DemoModule(int stateId,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               ControllerWirelessManager* controllerWirelessManager,
               bool skipDisconnectPolicy = false);
    ~DemoModule();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;

    // App-level score variables shared between GameState and ScoringState.
    int primaryScore = 0;
    int secondaryScore = 0;
    std::string primaryScoreLabel = "SCORE";
    std::string secondaryScoreLabel = "AVG MS";
    bool dualScoreDisplay = false;
    std::string thanksMessageLine2 = "PLAYING!";
    bool showFarewellMessage = true;

private:
    static constexpr int kMainMenuStateIndex = 0;
    DemoModuleDisconnectPolicy disconnectPolicy;
    ControllerWirelessManager* controllerWirelessManager;
    bool skipDisconnectPolicy_;
};
