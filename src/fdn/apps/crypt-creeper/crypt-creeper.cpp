#include "apps/crypt-creeper/crypt-creeper.hpp"
#include "apps/crypt-creeper/crypt-creeper-states.hpp"

CryptCreeper::CryptCreeper(int stateId,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator,
                       ControllerWirelessManager* controllerWirelessManager)
    : TypedStateMachine<FDN>(stateId)
    , disconnectPolicy(remoteDeviceCoordinator)
    , controllerWirelessManager(controllerWirelessManager) {}

CryptCreeper::~CryptCreeper() {}

void CryptCreeper::onStateMounted(Device* device) {
    if (!hasLaunched()) {
        TypedStateMachine<FDN>::onStateMounted(device);
        return;
    }
    skipToState(device, kMainMenuStateIndex);
}

void CryptCreeper::populateStateMap() {
    auto* mainMenuState = new CryptCreeperMainMenuState(controllerWirelessManager);
    auto* tutorialState = new CryptCreeperTutorialState(controllerWirelessManager);
    auto* gameState = new CryptCreeperGameState(controllerWirelessManager, &elapsedMs_);
    auto* scoringState = new CryptCreeperScoringState(controllerWirelessManager, &elapsedMs_);

    mainMenuState->addTransition(new StateTransition(
        std::bind(&CryptCreeperMainMenuState::transitionToTutorial, mainMenuState),
        tutorialState));
    mainMenuState->addTransition(new StateTransition(
        std::bind(&CryptCreeperMainMenuState::transitionToGame, mainMenuState),
        gameState));

    tutorialState->addTransition(new StateTransition(
        std::bind(&CryptCreeperTutorialState::transitionToMainMenu, tutorialState),
        mainMenuState));

    gameState->addTransition(new StateTransition(
        std::bind(&CryptCreeperGameState::transitionToScoring, gameState),
        scoringState));

    scoringState->addTransition(new StateTransition(
        std::bind(&CryptCreeperScoringState::transitionToMainMenu, scoringState),
        mainMenuState));

    stateMap.push_back(mainMenuState);
    stateMap.push_back(tutorialState);
    stateMap.push_back(gameState);
    stateMap.push_back(scoringState);

    for (State* state : stateMap) {
        state->addAppTransition(
            [this]() { return disconnectPolicy.isPersistentlyDisconnected(); },
            StateId(SYMBOL_LOCK_APP_ID));
    }
}
