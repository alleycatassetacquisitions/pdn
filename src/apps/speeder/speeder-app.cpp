#include "apps/speeder/speeder-app.hpp"

SpeederApp::SpeederApp() : StateMachine(SPEEDER_APP_ID) {
    instructionsConfig.pages = instructionsPages;
    instructionsConfig.pageCount = 3;
}

SpeederApp::~SpeederApp() {
    for(auto state : stateMap) {
        delete state;
    }
    stateMap.clear();
}

void SpeederApp::populateStateMap() {

    InstructionsState* tutorialState = new InstructionsState(INSTRUCTIONS, instructionsConfig);
    Speeding* speedingState = new Speeding();
    GameOver* gameOverState = new GameOver();
    InstructionsState* tryAgainState = new InstructionsState(GAME_OVER, tryAgainConfig);

    tutorialState->addTransition(new StateTransition(
        std::bind(&InstructionsState::optionOneSelectedEvent, tutorialState),
        speedingState
    ));
    tutorialState->addTransition(new StateTransition(
        std::bind(&InstructionsState::optionTwoSelectedEvent, tutorialState),
        tutorialState
    ));

    speedingState->addTransition(new StateTransition(
        std::bind(&Speeding::gameOver, speedingState),
        gameOverState
    ));

    gameOverState->addTransition(new StateTransition(
        std::bind(&GameOver::shouldTransitionToTryAgain, gameOverState),
        tryAgainState
    ));

    tryAgainState->addTransition(new StateTransition(
        std::bind(&InstructionsState::optionOneSelectedEvent, tryAgainState),
        speedingState
    ));

    tryAgainState->addTransition(new StateTransition(
        std::bind(&InstructionsState::optionTwoSelectedEvent, tryAgainState),
        tutorialState
    ));


    stateMap.push_back(tutorialState);
    stateMap.push_back(speedingState);
    stateMap.push_back(gameOverState);
    stateMap.push_back(tryAgainState);
}