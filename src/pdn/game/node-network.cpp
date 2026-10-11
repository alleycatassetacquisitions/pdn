#include "game/node-network.hpp"
#include "device/drivers/logger.hpp"
#include <functional>

NodeNetwork::NodeNetwork(Player* player, Device* pdn, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager)
    : StateMachine(NODE_NETWORK_APP_ID) {
    this->player = player;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    this->remoteDebugManager = remoteDebugManager;
    this->wirelessManager = pdn->getWirelessManager();
    this->storageManager = pdn->getStorage();
    this->remoteDeviceCoordinator = pdn->getRemoteDeviceCoordinator();

    matchManager = new NodeMatchManager();
    matchManager->initialize(player, storageManager, quickdrawWirelessManager);
    matchManager->setRemoteDeviceCoordinator(remoteDeviceCoordinator);

    quickdrawWirelessManager->setPacketReceivedCallback(
        std::bind(&NodeMatchManager::listenForMatchEvents, matchManager, std::placeholders::_1));
}

NodeNetwork::~NodeNetwork() {
    if (matchManager != nullptr) {
        delete matchManager;
        matchManager = nullptr;
    }
    player = nullptr;
    wirelessManager = nullptr;
    storageManager = nullptr;
    remoteDeviceCoordinator = nullptr;
    quickdrawWirelessManager = nullptr;
    remoteDebugManager = nullptr;
}

void NodeNetwork::populateStateMap() {
    PlayerRegistrationApp* playerRegistration = new PlayerRegistrationApp(player, wirelessManager, matchManager, remoteDebugManager);

    NodeAwakenSequence* awakenSequence = new NodeAwakenSequence(player);
    NodeIdle* idle = new NodeIdle(player, matchManager, remoteDeviceCoordinator);

    NodeDuelCountdown* duelCountdown = new NodeDuelCountdown(player, matchManager, remoteDeviceCoordinator);
    NodeDuel* duel = new NodeDuel(player, matchManager, remoteDeviceCoordinator);
    NodeDuelPushed* duelPushed = new NodeDuelPushed(player, matchManager, remoteDeviceCoordinator);
    NodeDuelReceivedResult* duelReceivedResult = new NodeDuelReceivedResult(player, matchManager, remoteDeviceCoordinator);
    NodeDuelResult* duelResult = new NodeDuelResult(player, matchManager);
    NodeWin* win = new NodeWin(player, matchManager);
    NodeLinkRetry* linkRetry = new NodeLinkRetry(player, matchManager);
    NodeSleep* sleep = new NodeSleep(player);
    NodeUploadMatches* uploadMatches = new NodeUploadMatches(player, wirelessManager, matchManager);

    playerRegistration->addTransition(
        new StateTransition(
            std::bind(&PlayerRegistrationApp::readyForGameplay, playerRegistration),
            awakenSequence));

    awakenSequence->addTransition(
        new StateTransition(
            std::bind(&NodeAwakenSequence::transitionToIdle, awakenSequence),
            idle));

    idle->addTransition(
        new StateTransition(
            std::bind(&NodeIdle::transitionToDuelCountdown, idle),
            duelCountdown));

    duelCountdown->addTransition(
        new StateTransition(
            std::bind(&NodeDuelCountdown::shallWeBattle, duelCountdown),
            duel));

    duelCountdown->addTransition(
        new StateTransition(
            std::bind(&NodeDuelCountdown::disconnectedBackToIdle, duelCountdown),
            idle));

    duel->addTransition(
        new StateTransition(
            std::bind(&NodeDuel::transitionToIdle, duel),
            idle));

    duel->addTransition(
        new StateTransition(
            std::bind(&NodeDuel::transitionToDuelReceivedResult, duel),
            duelReceivedResult));

    duel->addTransition(
        new StateTransition(
            std::bind(&NodeDuel::transitionToDuelPushed, duel),
            duelPushed));

    duelPushed->addTransition(
        new StateTransition(
            std::bind(&NodeDuelPushed::disconnectedBackToIdle, duelPushed),
            idle));

    duelPushed->addTransition(
        new StateTransition(
            std::bind(&NodeDuelPushed::transitionToDuelResult, duelPushed),
            duelResult));

    duelReceivedResult->addTransition(
        new StateTransition(
            std::bind(&NodeDuelReceivedResult::disconnectedBackToIdle, duelReceivedResult),
            idle));

    duelReceivedResult->addTransition(
        new StateTransition(
            std::bind(&NodeDuelReceivedResult::transitionToDuelResult, duelReceivedResult),
            duelResult));

    duelResult->addTransition(
        new StateTransition(
            std::bind(&NodeDuelResult::transitionToWin, duelResult),
            win));

    duelResult->addTransition(
        new StateTransition(
            std::bind(&NodeDuelResult::transitionToLinkRetry, duelResult),
            linkRetry));

    linkRetry->addTransition(
        new StateTransition(
            std::bind(&NodeLinkRetry::transitionToIdle, linkRetry),
            idle));

    win->addTransition(
        new StateTransition(
            std::bind(&NodeWin::resetGame, win),
            uploadMatches));

    uploadMatches->addTransition(
        new StateTransition(
            std::bind(&NodeUploadMatches::transitionToSleep, uploadMatches),
            sleep));

    sleep->addTransition(
        new StateTransition(
            std::bind(&NodeSleep::transitionToAwakenSequence, sleep),
            awakenSequence));

    stateMap.push_back(playerRegistration);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(duelPushed);
    stateMap.push_back(duelReceivedResult);
    stateMap.push_back(duelResult);
    stateMap.push_back(win);
    stateMap.push_back(linkRetry);
    stateMap.push_back(uploadMatches);
    stateMap.push_back(sleep);
}
