#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/progress-manager.hpp"
#include "game/fdn-result-manager.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "wireless/remote-debug-manager.hpp"

constexpr size_t MATCH_SIZE = sizeof(Match);

constexpr int QUICKDRAW_APP_ID = 1;

class Quickdraw : public StateMachine {
public:
    Quickdraw(Player *player, Device *PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager);
    ~Quickdraw();

    void populateStateMap() override;
    static Image getImageForAllegiance(Allegiance allegiance, ImageType whichImage);
    ProgressManager* getProgressManager() const { return progressManager; }

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
    ProgressManager* progressManager = nullptr;
    FdnResultManager* fdnResultManager = nullptr;
};
