#include "game/quickdraw-states/konami-button-awarded.hpp"
#include "game/progress-manager.hpp"
#include "device/device.hpp"
#include "device/device-constants.hpp"
#include <cstdio>

// State ID for KonamiButtonAwarded (use unique ID not conflicting with QuickdrawStateId)
constexpr int KONAMI_BUTTON_AWARDED_STATE_ID = 100;

KonamiButtonAwarded::KonamiButtonAwarded(GameType gameType, Player* player, ProgressManager* progressManager) :
    State(KONAMI_BUTTON_AWARDED_STATE_ID),
    gameType(gameType),
    player(player),
    progressManager(progressManager)
{
}

KonamiButtonAwarded::~KonamiButtonAwarded() {
    player = nullptr;
    progressManager = nullptr;
}

void KonamiButtonAwarded::onStateMounted(Device* PDN) {
    // Unlock the Konami button for this game
    KonamiButton button = getRewardForGame(gameType);
    uint8_t buttonIndex = static_cast<uint8_t>(button);
    player->unlockKonamiButton(buttonIndex);

    // Persist progress to NVS
    if (progressManager) {
        progressManager->saveProgress();
    }

    // Start victory timer
    victoryTimer.setTimer(VICTORY_DISPLAY_DURATION_MS);

    // Render initial victory screen
    displayIsDirty = true;

    // Victory haptic feedback - strong pulse
    PDN->getHaptics()->setIntensity(VIBRATION_MAX);

    // Victory LED animation (could be customized per game or use a standard pattern)
    AnimationConfig config;
    config.type = AnimationType::VERTICAL_CHASE;  // Victory animation
    config.loop = true;
    config.speed = 8;
    config.initialState = LEDState();
    config.loopDelayMs = 0;
    PDN->getLightManager()->startAnimation(config);
}

void KonamiButtonAwarded::onStateLoop(Device* PDN) {
    victoryTimer.updateTime();

    // Render victory screen if display is dirty
    if (displayIsDirty) {
        renderVictoryScreen(PDN);
        displayIsDirty = false;
    }

    // Turn off haptics after 200ms pulse
    static int hapticOffTime = 200;
    if (victoryTimer.getElapsedTime() > hapticOffTime) {
        PDN->getHaptics()->setIntensity(VIBRATION_OFF);
    }

    // Transition to GameOverReturnIdle after timeout
    if (victoryTimer.expired()) {
        transitionToGameOverReturnIdleState = true;
    }
}

void KonamiButtonAwarded::onStateDismounted(Device* PDN) {
    victoryTimer.invalidate();
    transitionToGameOverReturnIdleState = false;
    displayIsDirty = true;
    PDN->getHaptics()->setIntensity(VIBRATION_OFF);
    PDN->getLightManager()->stopAnimation();
}

bool KonamiButtonAwarded::transitionToGameOverReturnIdle() {
    return transitionToGameOverReturnIdleState;
}

void KonamiButtonAwarded::renderVictoryScreen(Device* PDN) {
    Display* display = PDN->getDisplay();
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Title: "BUTTON UNLOCKED!"
    display->drawText("BUTTON UNLOCKED!", 5, 12);

    // Game name
    const char* gameName = getGameDisplayName(gameType);
    display->drawText(gameName, 5, 27);

    // Button name
    KonamiButton button = getRewardForGame(gameType);
    const char* buttonName = getKonamiButtonName(button);
    char buttonBuf[32];
    snprintf(buttonBuf, sizeof(buttonBuf), "Button: %s", buttonName);
    display->drawText(buttonBuf, 5, 42);

    // Progress counter: "X of 7 collected"
    int unlockedCount = countUnlockedButtons();
    char progressBuf[32];
    snprintf(progressBuf, sizeof(progressBuf), "%d of 7 collected", unlockedCount);
    display->drawText(progressBuf, 5, 57);

    display->render();
}

int KonamiButtonAwarded::countUnlockedButtons() const {
    int count = 0;
    for (uint8_t i = 0; i < 7; i++) {
        if (player->hasUnlockedButton(i)) {
            count++;
        }
    }
    return count;
}
