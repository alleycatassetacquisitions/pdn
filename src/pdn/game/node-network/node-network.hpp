#pragma once

#include "apps/player-fetch/player-fetch.hpp"
#include "device/device.hpp"
#include "game/node-network/match-manager-v2.hpp"
#include "game/node-network/node-network-states.hpp"
#include "game/player-provider.hpp"
#include "state/state-machine.hpp"
#include "wireless/node-network-wireless-manager.hpp"

constexpr int NODE_NETWORK_APP_ID = 3;

/** Event app: embedded PlayerFetch, then NodeNetwork gameplay driven by MatchManagerV2. */
class NodeNetwork : public StateMachine {
public:
    /** Owns the player provider and match manager; routes NodeNetwork duel packets to the manager. */
    NodeNetwork(Device* pdn, NodeNetworkWirelessManager* nodeNetworkWirelessManager);
    ~NodeNetwork() override;

    void populateStateMap() override;

    PlayerProvider* getPlayerProvider() const;
    MatchManagerV2* getMatchManager() const;

private:
    Device* pdn = nullptr;
    NodeNetworkWirelessManager* nodeNetworkWirelessManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
    MatchManagerV2* matchManager = nullptr;
};
