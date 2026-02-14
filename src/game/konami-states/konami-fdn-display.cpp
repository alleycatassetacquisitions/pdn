#include "game/konami-states/konami-fdn-display.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/progress-manager.hpp"
#include <cstdlib>
#include <ctime>

static const char* TAG = "KonamiFdnDisplay";

KonamiFdnDisplay::KonamiFdnDisplay(Player* player) :
    State(static_cast<int>(KonamiFdnStateId::KONAMI_FDN_DISPLAY)),
    player(player)
{
}

KonamiFdnDisplay::~KonamiFdnDisplay() {
    player = nullptr;
}

void KonamiFdnDisplay::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Konami FDN Display mounted — the 8th node");

    transitionToKonamiCodeEntryState = false;
    displayIsDirty = true;

    // Initialize random seed for symbol cycling
    srand(static_cast<unsigned>(time(nullptr)));

    // Initialize position cycling states with randomized intervals
    initializePositions();

    // Check if player has all 7 buttons
    int buttonCount = getUnlockedButtonCount();
    LOG_I(TAG, "Player has %d of 7 buttons collected", buttonCount);

    if (buttonCount >= 7) {
        // All buttons collected — set up DOWN button callback
        parameterizedCallbackFunction downCallback = [](void* ctx) {
            auto* state = static_cast<KonamiFdnDisplay*>(ctx);
            LOG_I(TAG, "All buttons collected — transitioning to Konami Code Entry");
            state->transitionToKonamiCodeEntryState = true;
        };
        PDN->getPrimaryButton()->setButtonPress(downCallback, this, ButtonInteraction::CLICK);
    }

    cycleTimer.setTimer(100);  // Update cycling every 100ms
    renderTimer.setTimer(50);  // Render updates every 50ms

    renderDisplay(PDN);
}

void KonamiFdnDisplay::onStateLoop(Device* PDN) {
    cycleTimer.updateTime();
    renderTimer.updateTime();

    // Update cycling symbols for unlocked positions
    if (cycleTimer.expired()) {
        updateCyclingSymbols(PDN);
        cycleTimer.setTimer(100);
    }

    // Re-render if needed
    if (renderTimer.expired() && displayIsDirty) {
        renderDisplay(PDN);
        displayIsDirty = false;
        renderTimer.setTimer(50);
    }
}

void KonamiFdnDisplay::onStateDismounted(Device* PDN) {
    cycleTimer.invalidate();
    renderTimer.invalidate();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool KonamiFdnDisplay::transitionToKonamiCodeEntry() {
    return transitionToKonamiCodeEntryState;
}

void KonamiFdnDisplay::initializePositions() {
    for (int i = 0; i < POSITION_COUNT; i++) {
        positions[i].currentSymbolIndex = getRandomSymbolIndex();
        // Randomized cycle speed: 200-800ms
        positions[i].cycleIntervalMs = 200 + (rand() % 600);
        positions[i].lastCycleTime = 0;
    }
}

void KonamiFdnDisplay::updateCyclingSymbols(Device* PDN) {
    unsigned long currentTime = SimpleTimer::getPlatformClock()->milliseconds();
    bool anyChanged = false;

    for (int i = 0; i < POSITION_COUNT; i++) {
        // Only cycle symbols for locked (unrevealed) positions
        if (isPositionUnlocked(i)) {
            continue;
        }

        if (positions[i].lastCycleTime == 0) {
            positions[i].lastCycleTime = currentTime;
        }

        if (currentTime - positions[i].lastCycleTime >= static_cast<unsigned long>(positions[i].cycleIntervalMs)) {
            positions[i].currentSymbolIndex = (positions[i].currentSymbolIndex + 1) % SYMBOL_COUNT;
            positions[i].lastCycleTime = currentTime;
            anyChanged = true;
        }
    }

    if (anyChanged) {
        displayIsDirty = true;
    }
}

void KonamiFdnDisplay::renderDisplay(Device* PDN) {
    int buttonCount = getUnlockedButtonCount();

    if (buttonCount == 0) {
        renderIdleMode(PDN);
    } else if (buttonCount >= 7) {
        renderFullReveal(PDN);
    } else {
        renderPartialReveal(PDN);
    }
}

void KonamiFdnDisplay::renderIdleMode(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    // Title
    PDN->getDisplay()->drawText("??? NODE ???", 15, 5);

    // Draw cycling symbols in a grid pattern
    // Row 1: positions 0-4
    for (int i = 0; i < 5; i++) {
        const char* symbol = CRYPTIC_SYMBOLS[positions[i].currentSymbolIndex];
        PDN->getDisplay()->drawText(symbol, 10 + i * 20, 20);
    }

    // Row 2: positions 5-9
    for (int i = 5; i < 10; i++) {
        const char* symbol = CRYPTIC_SYMBOLS[positions[i].currentSymbolIndex];
        PDN->getDisplay()->drawText(symbol, 10 + (i - 5) * 20, 35);
    }

    // Row 3: positions 10-12
    for (int i = 10; i < 13; i++) {
        const char* symbol = CRYPTIC_SYMBOLS[positions[i].currentSymbolIndex];
        PDN->getDisplay()->drawText(symbol, 10 + (i - 10) * 20, 50);
    }

    PDN->getDisplay()->render();
}

void KonamiFdnDisplay::renderPartialReveal(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    int buttonCount = getUnlockedButtonCount();

    // Title
    PDN->getDisplay()->drawText("KONAMI NODE", 10, 5);

    // Progress indicator
    char progressBuf[32];
    snprintf(progressBuf, sizeof(progressBuf), "%d of 7", buttonCount);
    PDN->getDisplay()->drawText(progressBuf, 40, 18);

    // Draw symbols — revealed or cycling
    // Row 1: positions 0-4
    for (int i = 0; i < 5; i++) {
        const char* symbol = getSymbolForPosition(i);
        PDN->getDisplay()->drawText(symbol, 10 + i * 20, 32);
    }

    // Row 2: positions 5-9
    for (int i = 5; i < 10; i++) {
        const char* symbol = getSymbolForPosition(i);
        PDN->getDisplay()->drawText(symbol, 10 + (i - 5) * 20, 45);
    }

    // Row 3: positions 10-12
    for (int i = 10; i < 13; i++) {
        const char* symbol = getSymbolForPosition(i);
        PDN->getDisplay()->drawText(symbol, 10 + (i - 10) * 20, 58);
    }

    PDN->getDisplay()->render();
}

void KonamiFdnDisplay::renderFullReveal(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    // "K O N A M I" header
    PDN->getDisplay()->drawText("K O N A M I", 10, 5);

    // Draw the full revealed sequence
    // Row 1: positions 0-4
    for (int i = 0; i < 5; i++) {
        const char* symbol = getSymbolForPosition(i);
        PDN->getDisplay()->drawText(symbol, 10 + i * 20, 22);
    }

    // Row 2: positions 5-9
    for (int i = 5; i < 10; i++) {
        const char* symbol = getSymbolForPosition(i);
        PDN->getDisplay()->drawText(symbol, 10 + (i - 5) * 20, 35);
    }

    // Row 3: positions 10-12
    for (int i = 10; i < 13; i++) {
        const char* symbol = getSymbolForPosition(i);
        PDN->getDisplay()->drawText(symbol, 10 + (i - 10) * 20, 48);
    }

    // Prompt to begin
    PDN->getDisplay()->drawText("[DOWN to begin]", 5, 60);

    PDN->getDisplay()->render();
}

bool KonamiFdnDisplay::isPositionUnlocked(int position) const {
    // Check each button mapping to see if this position is unlocked
    for (const auto& mapping : BUTTON_MAPPINGS) {
        // Check if player has beaten this game (stub — needs ProgressManager integration)
        // For now, return false to show cycling symbols
        bool hasButton = false;  // TODO: Check player->hasKonamiButton(mapping.gameType)

        if (hasButton) {
            for (int i = 0; i < mapping.positionCount; i++) {
                if (mapping.positions[i] == position) {
                    return true;
                }
            }
        }
    }

    return false;
}

int KonamiFdnDisplay::getUnlockedButtonCount() const {
    int count = 0;

    // Count how many of the 7 games the player has beaten
    // This is a stub — needs ProgressManager integration
    // For now, return 0 to show idle mode

    // TODO: Integrate with ProgressManager to check:
    // - progressManager->hasBeatenGame(GameType::SIGNAL_ECHO)
    // - progressManager->hasBeatenGame(GameType::GHOST_RUNNER)
    // - etc. for all 7 games

    return count;
}

const char* KonamiFdnDisplay::getSymbolForPosition(int position) const {
    if (isPositionUnlocked(position)) {
        // Show the actual Konami button symbol
        KonamiButton button = KONAMI_SEQUENCE[position];
        switch (button) {
            case KonamiButton::UP:    return "↑";
            case KonamiButton::DOWN:  return "↓";
            case KonamiButton::LEFT:  return "←";
            case KonamiButton::RIGHT: return "→";
            case KonamiButton::B:     return "B";
            case KonamiButton::A:     return "A";
            case KonamiButton::START: return "★";
            default:                  return "?";
        }
    } else {
        // Show cycling cryptic symbol
        return CRYPTIC_SYMBOLS[positions[position].currentSymbolIndex];
    }
}

int KonamiFdnDisplay::getRandomSymbolIndex() const {
    return rand() % SYMBOL_COUNT;
}
