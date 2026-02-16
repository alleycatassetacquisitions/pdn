#include "game/konami-states/konami-reward-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/progress-manager.hpp"

static const char* TAG = "KonamiRewardStates";

// ==================== KmgButtonAwarded ====================

KmgButtonAwarded::KmgButtonAwarded(Player* player, ProgressManager* progressManager) :
    State(29),  // KONAMI_BUTTON_AWARDED index
    player(player),
    progressManager(progressManager),
    transitionToGameOverState(false),
    unlockedGameType(GameType::SIGNAL_ECHO)
{
}

KmgButtonAwarded::~KmgButtonAwarded() {
    player = nullptr;
    progressManager = nullptr;
}

void KmgButtonAwarded::onStateMounted(Device* PDN) {
    transitionToGameOverState = false;

    // Read pending reward from player (set by FdnDetected)
    unlockedGameType = static_cast<GameType>(player->getLastFdnGameType());
    KonamiButton button = getRewardForGame(unlockedGameType);

    LOG_I(TAG, "Button awarded: game=%d, button=%d",
          static_cast<int>(unlockedGameType), static_cast<int>(button));

    // Unlock the button
    player->unlockKonamiButton(static_cast<uint8_t>(button));

    // Save progress
    if (progressManager) {
        progressManager->saveProgress();
    }

    // Display celebration
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("BUTTON", 30, 15);
    PDN->getDisplay()->drawText("UNLOCKED!", 20, 30);

    // Show button symbol
    const char* buttonSymbol = "?";
    switch (button) {
        case KonamiButton::UP:    buttonSymbol = "UP"; break;
        case KonamiButton::DOWN:  buttonSymbol = "DOWN"; break;
        case KonamiButton::LEFT:  buttonSymbol = "LEFT"; break;
        case KonamiButton::RIGHT: buttonSymbol = "RIGHT"; break;
        case KonamiButton::B:     buttonSymbol = "B"; break;
        case KonamiButton::A:     buttonSymbol = "A"; break;
        case KonamiButton::START: buttonSymbol = "START"; break;
    }
    PDN->getDisplay()->drawText(buttonSymbol, 40, 45);
    PDN->getDisplay()->render();

    // TODO: Celebration LED pattern
    // PDN->getLightManager()->setAll(0, 255, 0);  // Green

    // TODO: Haptic celebration
    // PDN->getHaptics()->setIntensity(255);

    celebrationTimer.setTimer(CELEBRATION_DURATION_MS);
}

void KmgButtonAwarded::onStateLoop(Device* PDN) {
    celebrationTimer.updateTime();

    if (celebrationTimer.expired()) {
        transitionToGameOverState = true;
    }
}

void KmgButtonAwarded::onStateDismounted(Device* PDN) {
    transitionToGameOverState = false;
    celebrationTimer.invalidate();
    // PDN->getLightManager()->clear();
}

bool KmgButtonAwarded::transitionToGameOver() {
    return transitionToGameOverState;
}

// ==================== KonamiBoonAwarded ====================

KonamiBoonAwarded::KonamiBoonAwarded(Player* player, ProgressManager* progressManager) :
    State(30),  // KONAMI_BOON_AWARDED index
    player(player),
    progressManager(progressManager),
    transitionToGameOverState(false),
    unlockedGameType(GameType::SIGNAL_ECHO)
{
}

KonamiBoonAwarded::~KonamiBoonAwarded() {
    player = nullptr;
    progressManager = nullptr;
}

void KonamiBoonAwarded::onStateMounted(Device* PDN) {
    transitionToGameOverState = false;

    // Read pending reward from player (set by FdnDetected)
    unlockedGameType = static_cast<GameType>(player->getLastFdnGameType());

    LOG_I(TAG, "Boon awarded: game=%d", static_cast<int>(unlockedGameType));

    // Unlock the color profile
    player->addColorProfileEligibility(static_cast<int>(unlockedGameType));

    // Save progress
    if (progressManager) {
        progressManager->saveProgress();
    }

    // Display celebration
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("BOON", 35, 15);
    PDN->getDisplay()->drawText("UNLOCKED!", 20, 30);
    PDN->getDisplay()->drawText(getGameDisplayName(unlockedGameType), 10, 45);
    PDN->getDisplay()->render();

    // TODO: Celebration LED pattern (use color profile colors if available)
    // PDN->getLightManager()->setAll(255, 215, 0);  // Gold

    // TODO: Haptic celebration
    // PDN->getHaptics()->setIntensity(255);

    celebrationTimer.setTimer(CELEBRATION_DURATION_MS);
}

void KonamiBoonAwarded::onStateLoop(Device* PDN) {
    celebrationTimer.updateTime();

    if (celebrationTimer.expired()) {
        transitionToGameOverState = true;
    }
}

void KonamiBoonAwarded::onStateDismounted(Device* PDN) {
    transitionToGameOverState = false;
    celebrationTimer.invalidate();
    // PDN->getLightManager()->clear();
}

bool KonamiBoonAwarded::transitionToGameOver() {
    return transitionToGameOverState;
}

// ==================== KonamiGameOverReturn ====================

KonamiGameOverReturn::KonamiGameOverReturn(Player* player) :
    State(31),  // KONAMI_GAME_OVER_RETURN index
    player(player),
    transitionToReturnQuickdrawState(false)
{
}

KonamiGameOverReturn::~KonamiGameOverReturn() {
    player = nullptr;
}

void KonamiGameOverReturn::onStateMounted(Device* PDN) {
    transitionToReturnQuickdrawState = false;

    LOG_I(TAG, "GameOverReturn - returning to Quickdraw");

    // Display brief message
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("RETURNING...", 20, 30);
    PDN->getDisplay()->render();

    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void KonamiGameOverReturn::onStateLoop(Device* PDN) {
    displayTimer.updateTime();

    if (displayTimer.expired()) {
        // Return to Quickdraw app (app ID 0)
        LOG_I(TAG, "Switching to Quickdraw app");
        PDN->setActiveApp(StateId(0));
        transitionToReturnQuickdrawState = true;
    }
}

void KonamiGameOverReturn::onStateDismounted(Device* PDN) {
    transitionToReturnQuickdrawState = false;
    displayTimer.invalidate();
}

bool KonamiGameOverReturn::transitionToReturnQuickdraw() {
    return transitionToReturnQuickdrawState;
}
