#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "apps/player-registration/player-registration.hpp"
#include "apps/handshake/handshake.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "apps/app-registry.hpp"

constexpr size_t MATCH_SIZE = sizeof(Match);

class Quickdraw : public StateMachine {
public:
    Quickdraw(Player *player, Device *PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager);
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
    QuickdrawWirelessManager* quickdrawWirelessManager;
    RemoteDebugManager* remoteDebugManager;
};
