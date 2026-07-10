#include "apps/demo-modules/demo-module.hpp"
#include "apps/demo-modules/demo-module-states.hpp"

DemoModule::DemoModule(int stateId,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator,
                       ControllerWirelessManager* controllerWirelessManager,
                       bool skipDisconnectPolicy)
    : TypedStateMachine<FDN>(stateId)
    , disconnectPolicy(remoteDeviceCoordinator)
    , controllerWirelessManager(controllerWirelessManager)
    , skipDisconnectPolicy_(skipDisconnectPolicy) {}

DemoModule::~DemoModule() {}

void DemoModule::onStateMounted(Device* device) {
    if (!hasLaunched()) {
        TypedStateMachine<FDN>::onStateMounted(device);
        return;
    }
    skipToState(device, kMainMenuStateIndex);
}

void DemoModule::populateStateMap() {
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

    if (!skipDisconnectPolicy_) {
        for (State* state : stateMap) {
            state->addAppTransition(
                [this]() { return disconnectPolicy.isPersistentlyDisconnected(); },
                StateId(SYMBOL_LOCK_APP_ID));
        }
    }
}
