#include "game/quickdraw-states/game-over-return-idle.hpp"
#include "game/quickdraw.hpp"
#include "device/device.hpp"
#include "device/device-constants.hpp"

// State ID for GameOverReturnIdle (use unique ID not conflicting with QuickdrawStateId)
constexpr int GAME_OVER_RETURN_IDLE_STATE_ID = 101;

GameOverReturnIdle::GameOverReturnIdle() :
    State(GAME_OVER_RETURN_IDLE_STATE_ID)
{
}

GameOverReturnIdle::~GameOverReturnIdle() {
}

void GameOverReturnIdle::onStateMounted(Device* PDN) {
    // Start return timer
    returnTimer.setTimer(RETURN_DELAY_MS);

    // Render initial screen
    displayIsDirty = true;
    shouldReturnToIdle = false;

    // Stop any ongoing animations
    PDN->getLightManager()->stopAnimation();
    PDN->getHaptics()->setIntensity(VIBRATION_OFF);
}

void GameOverReturnIdle::onStateLoop(Device* PDN) {
    returnTimer.updateTime();

    // Render game over screen if display is dirty
    if (displayIsDirty) {
        renderGameOverScreen(PDN);
        displayIsDirty = false;
    }

    // After timeout, return to Quickdraw idle
    if (returnTimer.expired()) {
        shouldReturnToIdle = true;
        // Switch back to Quickdraw app (QUICKDRAW_APP_ID = 1)
        PDN->setActiveApp(StateId(QUICKDRAW_APP_ID));
    }
}

void GameOverReturnIdle::onStateDismounted(Device* PDN) {
    returnTimer.invalidate();
    displayIsDirty = true;
    shouldReturnToIdle = false;

    // Ensure cleanup
    PDN->getLightManager()->stopAnimation();
    PDN->getHaptics()->setIntensity(VIBRATION_OFF);
}

void GameOverReturnIdle::renderGameOverScreen(Device* PDN) {
    Display* display = PDN->getDisplay();
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Center "RETURNING..." message
    display->drawText("RETURNING...", 15, 32);

    display->render();
}
