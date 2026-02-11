#include "game/fdn-game.hpp"
#include "game/fdn-states.hpp"
#include "game/fdn-result-manager.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "FdnGame";

FdnGame::FdnGame(GameType gameType, KonamiButton reward) :
    StateMachine(FDN_GAME_APP_ID),
    gameType(gameType),
    reward(reward)
{
    LOG_I(TAG, "Creating FdnGame: %s (reward: %s)",
          getGameDisplayName(gameType), getKonamiButtonName(reward));
}

FdnGame::~FdnGame() {
    LOG_I(TAG, "Destroying FdnGame");
}

void FdnGame::populateStateMap() {
    NpcIdle* npcIdle = new NpcIdle(this);
    NpcHandshake* npcHandshake = new NpcHandshake(this);
    NpcGameActive* npcGameActive = new NpcGameActive(this);
    NpcReceiveResult* npcReceiveResult = new NpcReceiveResult(this, nullptr);

    npcIdle->addTransition(
        new StateTransition(
            std::bind(&NpcIdle::transitionToHandshake, npcIdle),
            npcHandshake));

    npcHandshake->addTransition(
        new StateTransition(
            std::bind(&NpcHandshake::transitionToGameActive, npcHandshake),
            npcGameActive));

    npcHandshake->addTransition(
        new StateTransition(
            std::bind(&NpcHandshake::transitionToIdle, npcHandshake),
            npcIdle));

    npcGameActive->addTransition(
        new StateTransition(
            std::bind(&NpcGameActive::transitionToReceiveResult, npcGameActive),
            npcReceiveResult));

    npcGameActive->addTransition(
        new StateTransition(
            std::bind(&NpcGameActive::transitionToIdle, npcGameActive),
            npcIdle));

    npcReceiveResult->addTransition(
        new StateTransition(
            std::bind(&NpcReceiveResult::transitionToIdle, npcReceiveResult),
            npcIdle));

    stateMap.push_back(npcIdle);
    stateMap.push_back(npcHandshake);
    stateMap.push_back(npcGameActive);
    stateMap.push_back(npcReceiveResult);
}
