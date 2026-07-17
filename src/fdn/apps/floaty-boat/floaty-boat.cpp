#include "apps/floaty-boat/floaty-boat.hpp"
#include "apps/floaty-boat/floaty-boat-states.hpp"

FloatyBoat::FloatyBoat(int stateId,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator,
                       ControllerWirelessManager* controllerWirelessManager)
    : TypedStateMachine<FDN>(stateId)
    , disconnectPolicy(remoteDeviceCoordinator)
    , controllerWirelessManager(controllerWirelessManager) {}

FloatyBoat::~FloatyBoat() {}

void FloatyBoat::onStateMounted(Device* device) {
    if (!hasLaunched()) {
        TypedStateMachine<FDN>::onStateMounted(device);
        return;
    }
    skipToState(device, kMainMenuStateIndex);
}

void FloatyBoat::populateStateMap() {
    auto* mainMenuState = new MainMenuState(controllerWirelessManager);
    auto* tutorialState = new TutorialState(controllerWirelessManager);
    auto* gameState = new GameState(controllerWirelessManager, &primaryScore);
    auto* scoringState = new ScoringState(controllerWirelessManager,
                                          &primaryScore,
                                          &primaryScoreLabel);

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
