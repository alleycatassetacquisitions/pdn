#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/color-profiles.hpp"
#include "game/progress-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"

static const char* TAG = "BoonAwarded";

BoonAwarded::BoonAwarded(Player* player, ProgressManager* progressManager) :
    State(BOON_AWARDED),
    player(player),
    progressManager(progressManager),
    unlockedGameType(GameType::SIGNAL_ECHO)
{
}

BoonAwarded::~BoonAwarded() {
    player = nullptr;
    progressManager = nullptr;
}

void BoonAwarded::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Boon awarded mounted");
    transitionToColorPromptState = false;

    // Get the game type that was just beaten in hard mode
    int gameTypeValue = player->getPendingProfileGame();
    if (gameTypeValue < 0) {
        LOG_W(TAG, "No pending profile game set");
        transitionToColorPromptState = true;
        return;
    }

    unlockedGameType = static_cast<GameType>(gameTypeValue);
    LOG_I(TAG, "Celebrating hard mode victory for: %s", getGameDisplayName(unlockedGameType));

    // Update color profile eligibility bitmask
    player->addColorProfileEligibility(gameTypeValue);

    // Persist to NVS immediately
    if (progressManager) {
        progressManager->saveProgress();
        LOG_I(TAG, "Progress saved to NVS");
    }

    // Start celebration timer
    celebrationTimer.setTimer(CELEBRATION_DURATION_MS);

    // Trigger haptic celebration
    PDN->getHaptics()->setIntensity(200);

    // Start LED animation showing the unlocked palette
    const LEDState& profileState = getColorProfileState(static_cast<int>(unlockedGameType));
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.loop = true;
    config.speed = 2;
    config.initialState = profileState;
    PDN->getLightManager()->startAnimation(config);

    // Render initial display
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("BOON UNLOCKED!", 10, 10);

    const char* paletteName = getGameDisplayName(unlockedGameType);
    PDN->getDisplay()->drawText(paletteName, 15, 28);
    PDN->getDisplay()->drawText("Equip from menu", 8, 44);
    PDN->getDisplay()->render();

    LOG_I(TAG, "Boon celebration started");
}

void BoonAwarded::onStateLoop(Device* PDN) {
    celebrationTimer.updateTime();

    // Transition after celebration duration
    if (celebrationTimer.expired()) {
        transitionToColorPromptState = true;
    }
}

void BoonAwarded::onStateDismounted(Device* PDN) {
    celebrationTimer.invalidate();

    // Stop animation and clear LEDs
    PDN->getLightManager()->stopAnimation();
    PDN->getLightManager()->clear();

    // Turn off haptics
    PDN->getHaptics()->off();

    LOG_I(TAG, "Boon celebration complete");
}

bool BoonAwarded::transitionToColorPrompt() {
    return transitionToColorPromptState;
}
