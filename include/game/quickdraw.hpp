#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "apps/player-registration/player-registration.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "apps/fdn-connect/fdn-connect-wireless-manager.hpp"
#include "apps/fdn-connect/fdn-connect.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"

constexpr size_t MATCH_SIZE = sizeof(Match);

constexpr int QUICKDRAW_APP_ID = 1;

class Quickdraw : public StateMachine {
public:
    Quickdraw(Player *player, Device *PDN);
    ~Quickdraw();

    void populateStateMap() override;

private:
    std::vector<Match> matches;
    int numMatches = 0;
    MatchManager* matchManager;
    Player *player;
    WirelessManager* wirelessManager;
    StorageInterface* storageManager;
    PeerCommsInterface* peerComms;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;
};
