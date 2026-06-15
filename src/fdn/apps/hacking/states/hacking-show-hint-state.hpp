#pragma once

#include "state/fdn-connect-state.hpp"
#include "device/fdn.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "utils/simple-timer.hpp"

enum HackingStateId {
    HACKING_HINT = 0,
};

class HackingShowHintState : public FDNConnectState {
public:
    HackingShowHintState(FDNConnectWirelessManager* fdnConnectWirelessManager,
                         HackedPlayersManager* hackedPlayersManager,
                         RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~HackingShowHintState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool shouldTransitionToIdle();

private:
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    HackedPlayersManager* hackedPlayersManager;
    bool transitionToIdle = false;
};
