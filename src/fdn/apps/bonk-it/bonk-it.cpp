#include "apps/bonk-it/bonk-it.hpp"
#include "apps/bonk-it/bonk-it-states.hpp"

BonkIt::BonkIt(int stateId,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator,
                       ControllerWirelessManager* controllerWirelessManager)
    : TypedStateMachine<FDN>(stateId)
    , disconnectPolicy(remoteDeviceCoordinator)
    , controllerWirelessManager(controllerWirelessManager) {}

BonkIt::~BonkIt() {}

void BonkIt::onStateMounted(Device* device) {
    if (!hasLaunched()) {
        TypedStateMachine<FDN>::onStateMounted(device);
        return;
    }
    skipToState(device, kMainMenuStateIndex);
}

void BonkIt::populateStateMap() {
    auto* mainMenuState = new MainMenuState(controllerWirelessManager);
    auto* tutorialState = new TutorialState();
    auto* gameState = new GameState();
    auto* scoringState = new ScoringState();

    mainMenuState->addTransition(new StateTransition(
        std::bind(&MainMenuState::transitionToTutorial, mainMenuState),
        tutorialState));
    mainMenuState->addTransition(new StateTransition(
        std::bind(&MainMenuState::transitionToGame, mainMenuState),
        gameState));

    tutorialState->addTransition(new StateTransition(
        std::bind(&TutorialState::transitionToMainMenu, tutorialState),
        mainMenuState));

    gameState->addTransition(new StateTransition(
        std::bind(&GameState::transitionToScoring, gameState),
        scoringState));

    scoringState->addTransition(new StateTransition(
        std::bind(&ScoringState::transitionToMainMenu, scoringState),
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
