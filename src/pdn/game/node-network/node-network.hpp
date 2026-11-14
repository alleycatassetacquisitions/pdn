#pragma once

#include "apps/player-fetch/player-fetch.hpp"
#include "device/device.hpp"
#include "game/node-network/match-manager-v2.hpp"
#include "game/node-network/node-network-stats.hpp"
#include "game/node-network/node-network-states.hpp"
#include "game/player-provider.hpp"
#include "state/state-machine.hpp"
#include "wireless/node-network-wireless-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"

constexpr int NODE_NETWORK_APP_ID = 3;

/** Event app: embedded PlayerFetch, then NodeNetwork gameplay driven by MatchManagerV2. */
class NodeNetwork : public StateMachine {
public:
    NodeNetwork(Device* pdn, NodeNetworkWirelessManager* nodeNetworkWirelessManager,
                SymbolWirelessManager* symbolWirelessManager);
    ~NodeNetwork() override;

    void populateStateMap() override;

    /** Resends due reliable packets, then runs the current state. */
    void onStateLoop(Device* pdn) override;

    PlayerProvider* getPlayerProvider() const;
    MatchManagerV2* getMatchManager() const;
    NodeNetworkStats* getStats() const;

private:
    Device* pdn = nullptr;
    NodeNetworkWirelessManager* nodeNetworkWirelessManager = nullptr;
    SymbolWirelessManager* symbolWirelessManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
    MatchManagerV2* matchManager = nullptr;
    NodeNetworkStats* nodeNetworkStats = nullptr;
};
