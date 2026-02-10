#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "wireless/remote-debug-manager.hpp"

class StateMachineManager;
class ProgressManager;

constexpr size_t MATCH_SIZE = sizeof(Match);

class Quickdraw : public StateMachine {
public:
    Quickdraw(Player *player, Device *PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager);
    ~Quickdraw();

    void populateStateMap() override;
    static Image getImageForAllegiance(Allegiance allegiance, ImageType whichImage);

    void setStateMachineManager(StateMachineManager* mgr) { smManager = mgr; }
    void setProgressManager(ProgressManager* mgr) { progressManager = mgr; }
    StateMachineManager* getStateMachineManager() const { return smManager; }
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
    StateMachineManager* smManager = nullptr;
    ProgressManager* progressManager = nullptr;
};
