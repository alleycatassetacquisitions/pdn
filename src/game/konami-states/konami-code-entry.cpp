#include "game/konami-states/konami-code-entry.hpp"
#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/quickdraw-resources.hpp"
#include <cstring>

static const char* TAG = "KonamiCodeEntry";

KonamiCodeEntry::KonamiCodeEntry(Player* player) :
    State(KONAMI_CODE_ENTRY),
    player(player)
{
}

KonamiCodeEntry::~KonamiCodeEntry() {
    player = nullptr;
}

void KonamiCodeEntry::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Konami Code Entry activated");

    transitionToAcceptedState = false;
    transitionToGameOverState = false;
    currentPosition = 0;
    selectedButtonIndex = 0;
    displayIsDirty = true;

    // Build list of unlocked buttons
    buildUnlockedButtons();

    // Start inactivity timer
    inactivityTimer.setTimer(INACTIVITY_TIMEOUT_MS);

    // Attach button callbacks
    PDN->getPrimaryButton()->setButtonPress([](void* ctx) {
        KonamiCodeEntry* state = (KonamiCodeEntry*)ctx;
        // Cycle through unlocked buttons
        if (!state->unlockedButtons.empty()) {
            state->selectedButtonIndex = (state->selectedButtonIndex + 1) % state->unlockedButtons.size();
            state->displayIsDirty = true;
            state->inactivityTimer.setTimer(state->INACTIVITY_TIMEOUT_MS);
        }
    }, this, ButtonInteraction::CLICK);

    PDN->getSecondaryButton()->setButtonPress([](void* ctx) {
        KonamiCodeEntry* state = (KonamiCodeEntry*)ctx;
        state->pendingButtonPress = true;
    }, this, ButtonInteraction::CLICK);

    // Initial display
    renderDisplay(PDN);
}

void KonamiCodeEntry::onStateLoop(Device* PDN) {
    inactivityTimer.updateTime();

    // Check for timeout
    if (inactivityTimer.expired()) {
        LOG_W(TAG, "Inactivity timeout - returning to idle");
        transitionToGameOverState = true;
        return;
    }

    // Handle pending button press
    if (pendingButtonPress) {
        pendingButtonPress = false;
        processButtonPress(PDN);
    }

    // Update display if dirty
    if (displayIsDirty) {
        renderDisplay(PDN);
        displayIsDirty = false;
    }
}

void KonamiCodeEntry::onStateDismounted(Device* PDN) {
    inactivityTimer.invalidate();

    // Remove button callbacks
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();

    // Turn off haptics
    PDN->getHaptics()->off();

    // Clear display
    PDN->getDisplay()->invalidateScreen();
}

bool KonamiCodeEntry::transitionToAccepted() {
    return transitionToAcceptedState;
}

bool KonamiCodeEntry::transitionToGameOver() {
    return transitionToGameOverState;
}

void KonamiCodeEntry::buildUnlockedButtons() {
    unlockedButtons.clear();
    for (uint8_t i = 0; i < 7; i++) {
        if (player->hasUnlockedButton(i)) {
            unlockedButtons.push_back(static_cast<KonamiButton>(i));
        }
    }

    // Ensure we have at least one button (should always be true if we reach this state)
    if (unlockedButtons.empty()) {
        LOG_E(TAG, "No unlocked buttons found!");
        transitionToGameOverState = true;
    }
}

void KonamiCodeEntry::renderDisplay(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    // Title
    PDN->getDisplay()->drawText("ENTER THE CODE", 5, 10);

    // Show the sequence (first 8 inputs on one line, using symbols)
    char sequenceLine[32] = "";
    for (int i = 0; i < 8 && i < SEQUENCE_LENGTH; i++) {
        if (i < currentPosition) {
            // Already entered - show the actual button
            strcat(sequenceLine, getButtonSymbol(targetSequence[i]));
        } else {
            // Not yet entered - show underscore
            strcat(sequenceLine, "_");
        }
        if (i < 7) strcat(sequenceLine, " ");
    }
    PDN->getDisplay()->drawText(sequenceLine, 10, 25);

    // Show remaining inputs on second line if needed
    if (SEQUENCE_LENGTH > 8) {
        char sequenceLine2[32] = "";
        for (int i = 8; i < SEQUENCE_LENGTH; i++) {
            if (i < currentPosition) {
                strcat(sequenceLine2, getButtonSymbol(targetSequence[i]));
            } else {
                strcat(sequenceLine2, "_");
            }
            if (i < SEQUENCE_LENGTH - 1) strcat(sequenceLine2, " ");
        }
        PDN->getDisplay()->drawText(sequenceLine2, 10, 35);
    }

    // Show progress counter
    char progress[32];
    snprintf(progress, sizeof(progress), "%d of %d", currentPosition, SEQUENCE_LENGTH);
    PDN->getDisplay()->drawText(progress, 40, 50);

    // Show current selection (cycling through unlocked buttons)
    if (!unlockedButtons.empty()) {
        char selection[32];
        snprintf(selection, sizeof(selection), "-> %s",
                 getButtonSymbol(unlockedButtons[selectedButtonIndex]));
        PDN->getDisplay()->drawText(selection, 10, 60);
    }

    PDN->getDisplay()->render();
}

void KonamiCodeEntry::processButtonPress(Device* PDN) {
    // Commit the selected button
    if (unlockedButtons.empty()) return;

    KonamiButton selected = unlockedButtons[selectedButtonIndex];
    KonamiButton expected = targetSequence[currentPosition];

    if (selected == expected) {
        // Correct input!
        currentPosition++;
        provideCorrectFeedback(PDN);

        if (currentPosition >= SEQUENCE_LENGTH) {
            // Sequence complete!
            LOG_I(TAG, "Konami code completed successfully!");
            transitionToAcceptedState = true;
        }
    } else {
        // Incorrect input
        LOG_W(TAG, "Incorrect input at position %d (expected %d, got %d)",
              currentPosition, static_cast<int>(expected), static_cast<int>(selected));
        resetSequence(PDN);
    }

    displayIsDirty = true;

    // Reset inactivity timer
    inactivityTimer.setTimer(INACTIVITY_TIMEOUT_MS);
}

void KonamiCodeEntry::provideCorrectFeedback(Device* PDN) {
    // Short haptic pulse
    PDN->getHaptics()->setIntensity(150);

    // Brief green flash
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.loop = false;
    config.speed = 0;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
    PDN->getLightManager()->startAnimation(config);
}

void KonamiCodeEntry::provideIncorrectFeedback(Device* PDN) {
    // Longer haptic pulse (error pattern)
    PDN->getHaptics()->setIntensity(255);

    // Red flash
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.loop = false;
    config.speed = 0;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
    PDN->getLightManager()->startAnimation(config);
}

void KonamiCodeEntry::resetSequence(Device* PDN) {
    currentPosition = 0;
    provideIncorrectFeedback(PDN);
}

const char* KonamiCodeEntry::getButtonSymbol(KonamiButton button) {
    switch (button) {
        case KonamiButton::UP:    return "^";
        case KonamiButton::DOWN:  return "v";
        case KonamiButton::LEFT:  return "<";
        case KonamiButton::RIGHT: return ">";
        case KonamiButton::B:     return "B";
        case KonamiButton::A:     return "A";
        case KonamiButton::START: return "S";
        default:                  return "?";
    }
}
