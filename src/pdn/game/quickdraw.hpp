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
#include "wireless/remote-debug-manager.hpp"
#include "wireless/wireless-transport.hpp"
#include "game/chain-manager.hpp"
#include "game/shootout-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"

constexpr size_t MATCH_SIZE = sizeof(Match);

constexpr int QUICKDRAW_APP_ID = 1;

class Quickdraw : public StateMachine {
public:
    Quickdraw(Player *player, Device *PDN, RemoteDebugManager* remoteDebugManager, SymbolWirelessManager* symbolWirelessManager, WirelessTransport* transport);
    ~Quickdraw();

    void populateStateMap() override;

    void onStateLoop(Device *PDN) override;

private:
    std::vector<Match> matches;
    int numMatches = 0;
    MatchManager* matchManager;
    Player *player;
    WirelessManager* wirelessManager;
    StorageInterface* storageManager;
    PeerCommsInterface* peerComms;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolWirelessManager* symbolWirelessManager;
    RemoteDebugManager* remoteDebugManager;
    SupporterReady* supporterReadyState = nullptr;
    ChainManager* chainManager = nullptr;
    ShootoutManager* shootoutManager_ = nullptr;

    // Emits one STATS LOG_W line every kStatsLogIntervalMs with retry counters
    // from ChainManager, for a rolling view of retry-machine health off the
    // serial port.
    SimpleTimer statsLogTimer_;
    static constexpr unsigned long kStatsLogIntervalMs = 5000;

    // Diagnostic: track isCoordinator() transitions to expose ring closure
    // and tiebreaker handoffs (lower-mac demotion path).
    bool lastIsCoordinator_ = false;
    int lastLoggedStateId_ = -999;
};
