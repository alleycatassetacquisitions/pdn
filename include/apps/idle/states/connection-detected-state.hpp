#pragma once

#include <string>
#include "state/connect-state.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "utils/simple-timer.hpp"

class ConnectionDetectedState : public ConnectState {
public:
    explicit ConnectionDetectedState(HackedPlayersManager* hackedPlayersManager,
                                     FDNConnectWirelessManager* fdnConnectWirelessManager,
                                     RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~ConnectionDetectedState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;

    // Called by idle.cpp's connect handler before this state is entered
    void receivePdnConnection(const std::string& playerId);

    bool transitionToAuth();
    bool transitionToUnauthorized();

private:
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    SimpleTimer glyphTimer;
    bool contentReady = false;
    bool wasHacked = false;
    std::string pendingPlayerId;
};
