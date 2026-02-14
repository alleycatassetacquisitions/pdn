#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "KonamiPuzzle";

// Moved from header to implementation
static constexpr int SUCCESS_DISPLAY_MS = 3000;
static constexpr int ERROR_DISPLAY_MS = 2000;

KonamiPuzzle::KonamiPuzzle(Player* player) :
    State(KONAMI_PUZZLE)
{
    this->player = player;
}

KonamiPuzzle::~KonamiPuzzle() {
    player = nullptr;
}

void KonamiPuzzle::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    displayIsDirty = true;
    puzzleCompleted = false;
    errorShown = false;
    currentPosition = 0;
    selectedInputIndex = 0;

    LOG_I(TAG, "Konami Puzzle activated - all 7 buttons collected!");

    // Build the target Konami Code sequence
    buildTargetSequence();

    // Build the list of unlocked buttons
    buildUnlockedButtons();

    // Clear entered sequence
    enteredSequence.clear();

    // Set up button callbacks
    parameterizedCallbackFunction primaryCallback = [](void* ctx) {
        auto* state = static_cast<KonamiPuzzle*>(ctx);
        if (state->puzzleCompleted || state->errorShown) return;

        // UP button: Stamp the currently selected input
        KonamiButton selectedButton = state->unlockedButtons[state->selectedInputIndex];
        state->enteredSequence.push_back(selectedButton);

        LOG_I(TAG, "Stamped %s at position %d",
              getKonamiButtonName(selectedButton), state->currentPosition);

        // Check if this input matches the target
        if (state->checkInput()) {
            state->currentPosition++;

            if (state->currentPosition >= static_cast<int>(state->targetSequence.size())) {
                // Puzzle complete!
                state->player->incrementKonamiAttempts();
                LOG_I(TAG, "KONAMI CODE COMPLETE! Attempts: %d", state->player->getKonamiAttempts());
                state->puzzleCompleted = true;
                state->awardKonamiBoon();
                state->displayTimer.setTimer(SUCCESS_DISPLAY_MS);
            }
        } else {
            // Wrong input - increment attempts and reset
            state->player->incrementKonamiAttempts();
            LOG_W(TAG, "Wrong input! Expected %s, got %s (Attempt %d)",
                  getKonamiButtonName(state->targetSequence[state->currentPosition]),
                  getKonamiButtonName(selectedButton),
                  state->player->getKonamiAttempts());
            state->errorShown = true;
            state->displayTimer.setTimer(ERROR_DISPLAY_MS);
        }

        state->displayIsDirty = true;
    };

    parameterizedCallbackFunction secondaryCallback = [](void* ctx) {
        auto* state = static_cast<KonamiPuzzle*>(ctx);
        if (state->puzzleCompleted || state->errorShown) return;

        // DOWN button: Cycle through available inputs
        state->selectedInputIndex = (state->selectedInputIndex + 1) % state->unlockedButtons.size();
        state->displayIsDirty = true;

        LOG_I(TAG, "Cycled to %s",
              getKonamiButtonName(state->unlockedButtons[state->selectedInputIndex]));
    };

    PDN->getPrimaryButton()->setButtonPress(secondaryCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(primaryCallback, this, ButtonInteraction::CLICK);

    // Initial render
    renderCodeEntry(PDN);
}

void KonamiPuzzle::onStateLoop(Device* PDN) {
    if (puzzleCompleted && displayTimer.expired()) {
        transitionToIdleState = true;
        return;
    }

    if (errorShown && displayTimer.expired()) {
        // Reset for retry
        errorShown = false;
        currentPosition = 0;
        enteredSequence.clear();
        selectedInputIndex = 0;
        displayIsDirty = true;
    }

    if (displayIsDirty) {
        if (puzzleCompleted) {
            renderSuccess(PDN);
        } else if (errorShown) {
            renderError(PDN);
        } else {
            renderCodeEntry(PDN);
        }
        displayIsDirty = false;
    }
}

void KonamiPuzzle::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();
    targetSequence.clear();
    enteredSequence.clear();
    unlockedButtons.clear();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool KonamiPuzzle::transitionToIdle() {
    return transitionToIdleState;
}

void KonamiPuzzle::buildTargetSequence() {
    // The classic Konami Code: UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A START
    targetSequence = {
        KonamiButton::UP,
        KonamiButton::UP,
        KonamiButton::DOWN,
        KonamiButton::DOWN,
        KonamiButton::LEFT,
        KonamiButton::RIGHT,
        KonamiButton::LEFT,
        KonamiButton::RIGHT,
        KonamiButton::B,
        KonamiButton::A,
        KonamiButton::START
    };
}

void KonamiPuzzle::buildUnlockedButtons() {
    unlockedButtons.clear();

    // Add all 7 buttons in order (player has all of them to reach this state)
    for (uint8_t i = 0; i < 7; i++) {
        unlockedButtons.push_back(static_cast<KonamiButton>(i));
    }
}

void KonamiPuzzle::renderCodeEntry(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    // Title
    PDN->getDisplay()->drawText("KONAMI CODE", 15, 5);

    // Show progress: how many inputs entered
    char progressBuf[32];
    snprintf(progressBuf, sizeof(progressBuf), "%d / 11", currentPosition);
    PDN->getDisplay()->drawText(progressBuf, 50, 20);

    // Show currently selected input (larger text)
    const char* buttonName = getKonamiButtonName(unlockedButtons[selectedInputIndex]);
    PDN->getDisplay()->drawText(buttonName, 35, 35);

    // Instructions
    PDN->getDisplay()->drawText("UP:   Cycle", 5, 50);
    PDN->getDisplay()->drawText("DOWN: Stamp", 5, 60);

    PDN->getDisplay()->render();
}

void KonamiPuzzle::renderSuccess(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    PDN->getDisplay()->drawText("SYSTEM", 30, 15);
    PDN->getDisplay()->drawText("CRACKED", 25, 35);

    // Display shame message based on attempt count
    uint16_t attempts = player->getKonamiAttempts();
    char shameBuf[64];
    if (attempts == 1) {
        snprintf(shameBuf, sizeof(shameBuf), "First try!");
        PDN->getDisplay()->drawText(shameBuf, 20, 50);
        PDN->getDisplay()->drawText("Impressive.", 15, 60);
    } else if (attempts <= 3) {
        snprintf(shameBuf, sizeof(shameBuf), "Not bad.");
        PDN->getDisplay()->drawText(shameBuf, 25, 50);
        snprintf(shameBuf, sizeof(shameBuf), "%d attempts.", attempts);
        PDN->getDisplay()->drawText(shameBuf, 20, 60);
    } else if (attempts <= 6) {
        snprintf(shameBuf, sizeof(shameBuf), "Took %d tries?", attempts);
        PDN->getDisplay()->drawText(shameBuf, 10, 50);
        PDN->getDisplay()->drawText("Really?", 30, 60);
    } else {
        snprintf(shameBuf, sizeof(shameBuf), "After %d tries", attempts);
        PDN->getDisplay()->drawText(shameBuf, 10, 45);
        PDN->getDisplay()->drawText("...finally.", 20, 55);
        PDN->getDisplay()->drawText("Embarrassing", 5, 63);
    }

    PDN->getDisplay()->render();

    // Haptic feedback
    PDN->getHaptics()->setIntensity(VIBRATION_MAX);
}

void KonamiPuzzle::renderError(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    PDN->getDisplay()->drawText("WRONG INPUT", 10, 20);
    PDN->getDisplay()->drawText("TRY AGAIN", 20, 40);

    PDN->getDisplay()->render();

    // Error feedback - haptic
    PDN->getHaptics()->setIntensity(VIBRATION_MAX);
}

bool KonamiPuzzle::checkInput() {
    if (enteredSequence.size() == 0) return false;

    KonamiButton lastEntered = enteredSequence.back();
    KonamiButton expected = targetSequence[currentPosition];

    return lastEntered == expected;
}

void KonamiPuzzle::awardKonamiBoon() {
    player->setKonamiBoon(true);
    LOG_I(TAG, "Konami Boon awarded to player %s", player->getUserID().c_str());
}
