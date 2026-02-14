#include "game/konami-states/konami-code-result.hpp"
#include "game/quickdraw-states.hpp"
#include "game/progress-manager.hpp"
#include "game/quickdraw.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/quickdraw-resources.hpp"

static const char* TAG = "KonamiCodeResult";

// ============================================================================
// KonamiCodeAccepted
// ============================================================================

KonamiCodeAccepted::KonamiCodeAccepted(Player* player, ProgressManager* progressManager) :
    State(KONAMI_CODE_ACCEPTED),
    player(player),
    progressManager(progressManager)
{
}

KonamiCodeAccepted::~KonamiCodeAccepted() {
    player = nullptr;
    progressManager = nullptr;
}

void KonamiCodeAccepted::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Konami code ACCEPTED - unlocking hard mode");

    transitionToReturnQuickdrawState = false;

    // Unlock hard mode (set bit 7 of konamiProgress)
    player->unlockHardMode();

    // Save progress to NVS
    if (progressManager) {
        progressManager->saveProgress();
    }

    // Display success message
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("You've unlocked", 5, 10);
    PDN->getDisplay()->drawText("Konami's Boon!", 10, 20);
    PDN->getDisplay()->drawText("", 0, 30);
    PDN->getDisplay()->drawText("Beat the FDNs", 10, 40);
    PDN->getDisplay()->drawText("again to steal", 10, 50);
    PDN->getDisplay()->drawText("their power!", 10, 60);
    PDN->getDisplay()->render();

    // Celebration LED animation (cycling rainbow)
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.loop = false;
    config.speed = 0;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(255, 215, 0), 255);  // Gold
    PDN->getLightManager()->startAnimation(config);

    // Celebration haptic pattern
    PDN->getHaptics()->setIntensity(255);

    // Start display timer
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void KonamiCodeAccepted::onStateLoop(Device* PDN) {
    displayTimer.updateTime();

    if (displayTimer.expired()) {
        transitionToReturnQuickdrawState = true;
    }
}

void KonamiCodeAccepted::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();

    // Turn off haptics
    PDN->getHaptics()->off();

    // Clear LEDs
    PDN->getLightManager()->stopAnimation();
    PDN->getLightManager()->clear();

    // Return to Quickdraw app
    PDN->setActiveApp(StateId(QUICKDRAW_APP_ID));
}

bool KonamiCodeAccepted::transitionToReturnQuickdraw() {
    return transitionToReturnQuickdrawState;
}

// ============================================================================
// KonamiCodeRejected
// ============================================================================

KonamiCodeRejected::KonamiCodeRejected(Player* player) :
    State(KONAMI_CODE_REJECTED),
    player(player)
{
}

KonamiCodeRejected::~KonamiCodeRejected() {
    player = nullptr;
}

void KonamiCodeRejected::onStateMounted(Device* PDN) {
    LOG_W(TAG, "Konami code REJECTED - not all buttons collected");

    transitionToReturnQuickdrawState = false;

    // Count collected buttons
    int buttonCount = 0;
    for (uint8_t i = 0; i < 7; i++) {
        if (player->hasUnlockedButton(i)) {
            buttonCount++;
        }
    }

    // Display rejection message
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("NOT READY", 25, 15);

    char progress[32];
    snprintf(progress, sizeof(progress), "%d of 7 buttons", buttonCount);
    PDN->getDisplay()->drawText(progress, 15, 30);
    PDN->getDisplay()->drawText("collected", 20, 40);

    PDN->getDisplay()->drawText("Find more FDNs...", 5, 55);
    PDN->getDisplay()->render();

    // Red LED to indicate failure
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.loop = false;
    config.speed = 0;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(255, 0, 0), 150);
    PDN->getLightManager()->startAnimation(config);

    // Start display timer
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void KonamiCodeRejected::onStateLoop(Device* PDN) {
    displayTimer.updateTime();

    if (displayTimer.expired()) {
        transitionToReturnQuickdrawState = true;
    }
}

void KonamiCodeRejected::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();

    // Clear LEDs
    PDN->getLightManager()->stopAnimation();
    PDN->getLightManager()->clear();

    // Return to Quickdraw app
    PDN->setActiveApp(StateId(QUICKDRAW_APP_ID));
}

bool KonamiCodeRejected::transitionToReturnQuickdraw() {
    return transitionToReturnQuickdrawState;
}

// ============================================================================
// GameOverReturnIdle
// ============================================================================

GameOverReturnIdle::GameOverReturnIdle(Player* player) :
    State(GAME_OVER_RETURN_IDLE),
    player(player)
{
}

GameOverReturnIdle::~GameOverReturnIdle() {
    player = nullptr;
}

void GameOverReturnIdle::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Game Over - returning to Idle");

    transitionToReturnQuickdrawState = false;

    // Display timeout message
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("TIMEOUT", 30, 30);
    PDN->getDisplay()->render();

    // Start display timer
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void GameOverReturnIdle::onStateLoop(Device* PDN) {
    displayTimer.updateTime();

    if (displayTimer.expired()) {
        transitionToReturnQuickdrawState = true;
    }
}

void GameOverReturnIdle::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();

    // Return to Quickdraw app
    PDN->setActiveApp(StateId(QUICKDRAW_APP_ID));
}

bool GameOverReturnIdle::transitionToReturnQuickdraw() {
    return transitionToReturnQuickdrawState;
}
