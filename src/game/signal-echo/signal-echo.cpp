#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

SignalEcho::SignalEcho(SignalEchoConfig config) :
    MiniGame(SIGNAL_ECHO_APP_ID, GameType::SIGNAL_ECHO, "SIGNAL ECHO"),
    config(config)
{
}

void SignalEcho::populateStateMap() {
    seedRng();

    EchoIntro* intro = new EchoIntro(this);
    EchoShowSequence* showSequence = new EchoShowSequence(this);
    EchoPlayerInput* playerInput = new EchoPlayerInput(this);
    EchoEvaluate* evaluate = new EchoEvaluate(this);
    EchoWin* win = new EchoWin(this);
    EchoLose* lose = new EchoLose(this);

    intro->addTransition(
        new StateTransition(
            std::bind(&EchoIntro::transitionToShowSequence, intro),
            showSequence));

    showSequence->addTransition(
        new StateTransition(
            std::bind(&EchoShowSequence::transitionToPlayerInput, showSequence),
            playerInput));

    playerInput->addTransition(
        new StateTransition(
            std::bind(&EchoPlayerInput::transitionToEvaluate, playerInput),
            evaluate));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&EchoEvaluate::transitionToShowSequence, evaluate),
            showSequence));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&EchoEvaluate::transitionToWin, evaluate),
            win));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&EchoEvaluate::transitionToLose, evaluate),
            lose));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&EchoWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&EchoLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);
    stateMap.push_back(showSequence);
    stateMap.push_back(playerInput);
    stateMap.push_back(evaluate);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void SignalEcho::resetGame() {
    MiniGame::resetGame();
    session.reset();
}

void SignalEcho::seedRng() {
    if (config.rngSeed != 0) {
        srand(static_cast<unsigned int>(config.rngSeed));
    } else {
        srand(static_cast<unsigned int>(0));
    }
}

std::vector<bool> SignalEcho::generateSequence(int length) {
    std::vector<bool> seq;
    for (int i = 0; i < length; i++) {
        seq.push_back(rand() % 2 == 0);
    }
    return seq;
}
