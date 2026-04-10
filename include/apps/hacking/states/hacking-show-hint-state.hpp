#pragma once

#include "state/connect-state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/device.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "utils/simple-timer.hpp"

class HackingShowHintState : public ConnectState {
public:
    HackingShowHintState(FDNConnectWirelessManager* fdnConnectWirelessManager,
                         HackedPlayersManager* hackedPlayersManager,
                         RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~HackingShowHintState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;

    bool shouldTransitionToIdle();

private:
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    HackedPlayersManager* hackedPlayersManager;

    bool transitionToIdle = false;
};
