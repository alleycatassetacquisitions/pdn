#include "game/node-network/node-network.hpp"

#include "device/drivers/peer-comms-types.hpp"
#include "utils/simple-timer.hpp"

NodeNetwork::NodeNetwork(Device* pdn, NodeNetworkWirelessManager* nodeNetworkWirelessManager)
    : StateMachine(NODE_NETWORK_APP_ID)
    , pdn(pdn)
    , nodeNetworkWirelessManager(nodeNetworkWirelessManager)
    , playerProvider(new PlayerProvider())
    , matchManager(new MatchManagerV2(playerProvider, pdn->getStorage(),
                                      nodeNetworkWirelessManager, pdn->getRemoteDeviceCoordinator(),
                                      SimpleTimer::getPlatformClock())) {
    nodeNetworkWirelessManager->initialize(pdn->getWirelessManager());
    pdn->getWirelessManager()->setEspNowPacketHandler(
        PktType::kNodeNetworkCommand,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<NodeNetworkWirelessManager*>(ctx)->processPacket(macAddress, data, dataLen);
        },
        nodeNetworkWirelessManager);
}

NodeNetwork::~NodeNetwork() {
    pdn->getWirelessManager()->clearEspNowPacketHandler(PktType::kNodeNetworkCommand);
    MatchManagerV2* ownedMatchManager = matchManager;
    matchManager = nullptr;
    delete ownedMatchManager;
    PlayerProvider* ownedPlayerProvider = playerProvider;
    playerProvider = nullptr;
    delete ownedPlayerProvider;
    nodeNetworkWirelessManager = nullptr;
    pdn = nullptr;
}

void NodeNetwork::populateStateMap() {
    PlayerFetchApp* playerFetch = new PlayerFetchApp(
        pdn->getWirelessManager(), pdn->getFirmwareUpdateManager(), playerProvider);
    NodeIdle* nodeIdle = new NodeIdle();

    playerFetch->addTransition(new StateTransition(
        [playerFetch]() { return playerFetch->readyForGameplay(); }, nodeIdle));

    stateMap.push_back(playerFetch);
    stateMap.push_back(nodeIdle);
}

PlayerProvider* NodeNetwork::getPlayerProvider() const {
    return playerProvider;
}

MatchManagerV2* NodeNetwork::getMatchManager() const {
    return matchManager;
}
