#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoPlayerInput::EchoPlayerInput(SignalEcho* game) : State(ECHO_PLAYER_INPUT) {
    this->game = game;
}

EchoPlayerInput::~EchoPlayerInput() {
    game = nullptr;
}

void EchoPlayerInput::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    displayIsDirty = true;

    // Set up button callbacks
    parameterizedCallbackFunction upCallback = [](void* ctx) {
        auto* state = static_cast<EchoPlayerInput*>(ctx);
        state->handleInput(true, nullptr);
    };

    parameterizedCallbackFunction downCallback = [](void* ctx) {
        auto* state = static_cast<EchoPlayerInput*>(ctx);
        state->handleInput(false, nullptr);
    };

    PDN->getPrimaryButton()->setButtonPress(upCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(downCallback, this, ButtonInteraction::CLICK);

    renderInputScreen(PDN);
}

void EchoPlayerInput::onStateLoop(Device* PDN) {
    if (displayIsDirty) {
        renderInputScreen(PDN);
        displayIsDirty = false;
    }

    auto& session = game->getSession();
    int seqLen = static_cast<int>(session.currentSequence.size());

    // Check if all inputs have been entered
    if (session.inputIndex >= seqLen) {
        transitionToEvaluateState = true;
        return;
    }

    // Check if mistakes exceeded allowed
    if (session.mistakes > game->getConfig().allowedMistakes) {
        transitionToEvaluateState = true;
        return;
    }
}

void EchoPlayerInput::onStateDismounted(Device* PDN) {
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getHaptics()->off();
}

bool EchoPlayerInput::transitionToEvaluate() {
    return transitionToEvaluateState;
}

void EchoPlayerInput::handleInput(bool isUp, Device* PDN) {
    auto& session = game->getSession();
    int seqLen = static_cast<int>(session.currentSequence.size());

    if (session.inputIndex >= seqLen) {
        return;  // Already completed input for this round
    }

    bool expected = session.currentSequence[session.inputIndex];

    if (isUp == expected) {
        // Correct input
        session.score += 100;
    } else {
        // Wrong input — count mistake, advance anyway (locked in)
        session.mistakes++;
    }

    // Always advance to next position
    session.inputIndex++;
    displayIsDirty = true;
}

void EchoPlayerInput::renderInputScreen(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();
    int seqLen = static_cast<int>(session.currentSequence.size());

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("YOUR TURN", 25, 12);

    // Build input progress string
    std::string inputProgress;
    for (int i = 0; i < seqLen; i++) {
        if (i < session.inputIndex) {
            bool expected = session.currentSequence[i];
            if (expected) {
                inputProgress += "^ ";
            } else {
                inputProgress += "v ";
            }
        } else {
            inputProgress += "_ ";
        }
    }

    PDN->getDisplay()->drawText(inputProgress.c_str(), 5, 35);

    // Round counter + life indicator
    int livesRemaining = config.allowedMistakes - session.mistakes;
    if (livesRemaining < 0) livesRemaining = 0;

    std::string roundInfo = "Round " + std::to_string(session.currentRound + 1) +
                            "/" + std::to_string(config.numSequences);

    std::string lives;
    for (int i = 0; i < config.allowedMistakes; i++) {
        if (i < livesRemaining) {
            lives += "* ";  // Remaining life (filled)
        } else {
            lives += "o ";  // Lost life (empty)
        }
    }

    PDN->getDisplay()->drawText(roundInfo.c_str(), 5, 55);
    PDN->getDisplay()->drawText(lives.c_str(), 80, 55);
    PDN->getDisplay()->render();
}
