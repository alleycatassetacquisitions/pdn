#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoEvaluate::EchoEvaluate(SignalEcho* game) : State(ECHO_EVALUATE) {
    this->game = game;
}

EchoEvaluate::~EchoEvaluate() {
    game = nullptr;
}

void EchoEvaluate::onStateMounted(Device* PDN) {
    transitionToShowSequenceState = false;
    transitionToWinState = false;
    transitionToLoseState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Check if player has exceeded allowed mistakes
    if (session.mistakes > config.allowedMistakes) {
        transitionToLoseState = true;
        return;
    }

    // Round completed successfully — advance
    session.currentRound++;

    // Check if all rounds completed
    if (session.currentRound >= config.numSequences) {
        transitionToWinState = true;
        return;
    }

    // Generate next sequence
    if (config.cumulative) {
        // Cumulative mode: append one more element
        session.currentSequence.push_back(rand() % 2 == 0);
    } else {
        // Fresh mode: completely new sequence
        session.currentSequence = game->generateSequence(config.sequenceLength);
    }

    // Reset input index for the new round
    session.inputIndex = 0;

    transitionToShowSequenceState = true;
}

void EchoEvaluate::onStateLoop(Device* PDN) {
    // Transitions are determined in onStateMounted — nothing to do here
}

void EchoEvaluate::onStateDismounted(Device* PDN) {
    transitionToShowSequenceState = false;
    transitionToWinState = false;
    transitionToLoseState = false;
}

bool EchoEvaluate::transitionToShowSequence() {
    return transitionToShowSequenceState;
}

bool EchoEvaluate::transitionToWin() {
    return transitionToWinState;
}

bool EchoEvaluate::transitionToLose() {
    return transitionToLoseState;
}
