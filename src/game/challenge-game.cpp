#include "game/challenge-game.hpp"
#include "game/challenge-states.hpp"
#include "game/challenge-result-manager.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "ChallengeGame";

ChallengeGame::ChallengeGame(Device* device, GameType gameType, KonamiButton reward) :
    StateMachine(device),
    gameType(gameType),
    reward(reward)
{
    LOG_I(TAG, "Creating ChallengeGame: %s (reward: %s)",
          getGameDisplayName(gameType), getKonamiButtonName(reward));
}

ChallengeGame::~ChallengeGame() {
    LOG_I(TAG, "Destroying ChallengeGame");
}

void ChallengeGame::populateStateMap() {
    ChallengeResultManager* resultManager = new ChallengeResultManager();
    resultManager->initialize(getDevice()->getStorage());

    NpcIdle* npcIdle = new NpcIdle(this);
    NpcHandshake* npcHandshake = new NpcHandshake(this);
    NpcGameActive* npcGameActive = new NpcGameActive(this);
    NpcReceiveResult* npcReceiveResult = new NpcReceiveResult(this, resultManager);

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
