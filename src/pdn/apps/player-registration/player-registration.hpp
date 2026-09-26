#pragma once

#include "state/state-machine.hpp"
#include "apps/player-registration/player-registration-states.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/player.hpp"
#include "device/device.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"

class PlayerRegistrationApp : public StateMachine {
public:
    /// Boot app: id entry, the record fetch, and the offline fallback that
    /// skips it, ending at the hand-off to gameplay.
    PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager);
    ~PlayerRegistrationApp();

    void populateStateMap() override;

private:
    Player* player;
    WirelessManager* wirelessManager;
    RemoteDebugManager* remoteDebugManager;
};
