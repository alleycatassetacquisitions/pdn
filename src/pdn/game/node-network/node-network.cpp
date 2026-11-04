#include "game/node-network/node-network.hpp"

#include "device/drivers/peer-comms-types.hpp"
#include "game/quickdraw-states.hpp"
#include "utils/simple-timer.hpp"

NodeNetwork::NodeNetwork(Device* pdn, NodeNetworkWirelessManager* nodeNetworkWirelessManager,
                         SymbolWirelessManager* symbolWirelessManager)
    : StateMachine(NODE_NETWORK_APP_ID)
    , pdn(pdn)
    , nodeNetworkWirelessManager(nodeNetworkWirelessManager)
    , symbolWirelessManager(symbolWirelessManager)
    , playerProvider(new PlayerProvider())
    , matchManager(new MatchManagerV2(playerProvider, pdn->getStorage(),
                                      nodeNetworkWirelessManager, pdn->getRemoteDeviceCoordinator(),
                                      SimpleTimer::getPlatformClock()))
    , nodeNetworkStats(new NodeNetworkStats(pdn->getStorage())) {
    nodeNetworkWirelessManager->initialize(pdn->getWirelessManager());
    pdn->getWirelessManager()->setEspNowPacketHandler(
        PktType::kNodeNetworkCommand,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<NodeNetworkWirelessManager*>(ctx)->processPacket(macAddress, data, dataLen);
        },
        nodeNetworkWirelessManager);

    symbolWirelessManager->initialize(pdn->getWirelessManager(), pdn->getRemoteDeviceCoordinator());
    pdn->getWirelessManager()->setEspNowPacketHandler(
        PktType::kSymbolMatchCommand,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<SymbolWirelessManager*>(ctx)->processSymbolMatchCommand(macAddress, data,
                                                                                dataLen);
        },
        symbolWirelessManager);
}

NodeNetwork::~NodeNetwork() {
    pdn->getWirelessManager()->clearEspNowPacketHandler(PktType::kNodeNetworkCommand);
    pdn->getWirelessManager()->clearEspNowPacketHandler(PktType::kSymbolMatchCommand);
    delete nodeNetworkStats;
    nodeNetworkStats = nullptr;
    delete matchManager;
    matchManager = nullptr;
    delete playerProvider;
    playerProvider = nullptr;
    symbolWirelessManager = nullptr;
    nodeNetworkWirelessManager = nullptr;
    pdn = nullptr;
}

void NodeNetwork::populateStateMap() {
    Player* player = playerProvider->getPlayer();
    RemoteDeviceCoordinator* rdc = pdn->getRemoteDeviceCoordinator();
    WirelessManager* wireless = pdn->getWirelessManager();

    PlayerFetchApp* playerFetch =
        new PlayerFetchApp(wireless, pdn->getFirmwareUpdateManager(), playerProvider);

    NodeIdle* idle = new NodeIdle(player, matchManager, nodeNetworkStats, rdc);
    NodeDuelCountdown* duelCountdown = new NodeDuelCountdown(matchManager, rdc);
    NodeDuel* duel = new NodeDuel(matchManager, rdc);
    NodeWin* win = new NodeWin(matchManager);
    NodeLinkRetry* linkRetry = new NodeLinkRetry(matchManager);
    NodeUploadMatches* upload = new NodeUploadMatches(wireless, matchManager, nodeNetworkStats);
    NodeSleep* sleep = new NodeSleep(matchManager);
    NodeAwaken* awaken = new NodeAwaken();

    SymbolState* symbol = new SymbolState(player, nullptr, rdc, symbolWirelessManager);
    SymbolMatched* symbolMatched = new SymbolMatched(player, rdc, symbolWirelessManager);

    playerFetch->addTransition(
        new StateTransition([playerFetch]() { return playerFetch->readyForGameplay(); }, idle));

    idle->addTransition(new StateTransition([idle]() { return idle->transitionToDuelCountdown(); },
                                            duelCountdown));
    idle->addTransition(
        new StateTransition([idle]() { return idle->transitionToSymbol(); }, symbol));

    duelCountdown->addTransition(
        new StateTransition([duelCountdown]() { return duelCountdown->shallWeBattle(); }, duel));
    duelCountdown->addTransition(new StateTransition(
        [duelCountdown]() { return duelCountdown->disconnectedBackToIdle(); }, idle));

    duel->addTransition(
        new StateTransition([duel]() { return duel->transitionToWin(); }, win));
    duel->addTransition(
        new StateTransition([duel]() { return duel->transitionToLinkRetry(); }, linkRetry));
    duel->addTransition(
        new StateTransition([duel]() { return duel->disconnectedBackToIdle(); }, idle));

    win->addTransition(new StateTransition([win]() { return win->transitionToUpload(); }, upload));

    linkRetry->addTransition(
        new StateTransition([linkRetry]() { return linkRetry->transitionToIdle(); }, idle));
    linkRetry->addTransition(
        new StateTransition([linkRetry]() { return linkRetry->transitionToSleep(); }, sleep));

    upload->addTransition(
        new StateTransition([upload]() { return upload->transitionToSleep(); }, sleep));

    sleep->addTransition(
        new StateTransition([sleep]() { return sleep->transitionToAwaken(); }, awaken));

    awaken->addTransition(new StateTransition([awaken]() { return awaken->transitionToIdle(); },
                                            idle));

    symbol->addTransition(
        new StateTransition([symbol]() { return symbol->transitionToIdle(); }, idle));
    symbol->addTransition(
        new StateTransition([symbol]() { return symbol->transitionToSymbolMatched(); },
                            symbolMatched));
    symbolMatched->addTransition(
        new StateTransition([symbolMatched]() { return symbolMatched->transitionToIdle(); },
                            idle));

    stateMap.push_back(playerFetch);
    stateMap.push_back(idle);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(win);
    stateMap.push_back(linkRetry);
    stateMap.push_back(upload);
    stateMap.push_back(sleep);
    stateMap.push_back(awaken);
    stateMap.push_back(symbol);
    stateMap.push_back(symbolMatched);
}

PlayerProvider* NodeNetwork::getPlayerProvider() const {
    return playerProvider;
}

MatchManagerV2* NodeNetwork::getMatchManager() const {
    return matchManager;
}

NodeNetworkStats* NodeNetwork::getStats() const {
    return nodeNetworkStats;
}
