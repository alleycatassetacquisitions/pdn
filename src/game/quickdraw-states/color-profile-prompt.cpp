#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/color-profiles.hpp"
#include "game/progress-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"

static const char* TAG = "ColorProfilePrompt";

ColorProfilePrompt::ColorProfilePrompt(Player* player, ProgressManager* progressManager) :
    State(COLOR_PROFILE_PROMPT),
    player(player),
    progressManager(progressManager)
{
}

ColorProfilePrompt::~ColorProfilePrompt() {
    player = nullptr;
    progressManager = nullptr;
}

void ColorProfilePrompt::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    selectYes = true;

    int gameType = player->getPendingProfileGame();
    LOG_I(TAG, "Color profile prompt for game type %d", gameType);

    // Set up button callbacks
    parameterizedCallbackFunction toggleCb = [](void* ctx) {
        auto* self = static_cast<ColorProfilePrompt*>(ctx);
        self->selectYes = !self->selectYes;
        self->renderUi(nullptr);  // Will be rendered on next loop
    };

    parameterizedCallbackFunction confirmCb = [](void* ctx) {
        auto* self = static_cast<ColorProfilePrompt*>(ctx);
        int gameType = self->player->getPendingProfileGame();

        if (self->selectYes && gameType >= 0) {
            self->player->setEquippedColorProfile(gameType);
            LOG_I(TAG, "Equipped color profile: %s",
                   getColorProfileName(gameType, self->player->isHunter()));
            if (self->progressManager) {
                self->progressManager->saveProgress();
            }
        } else {
            LOG_I(TAG, "Declined color profile");
        }
        self->transitionToIdleState = true;
    };

    PDN->getPrimaryButton()->setButtonPress(toggleCb, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);

    // Auto-dismiss timer
    dismissTimer.setTimer(AUTO_DISMISS_MS);

    renderUi(PDN);
}

void ColorProfilePrompt::onStateLoop(Device* PDN) {
    dismissTimer.updateTime();
    if (dismissTimer.expired()) {
        LOG_I(TAG, "Auto-dismissed (NO)");
        transitionToIdleState = true;
    }
}

void ColorProfilePrompt::onStateDismounted(Device* PDN) {
    dismissTimer.invalidate();
    player->setPendingProfileGame(-1);
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool ColorProfilePrompt::transitionToIdle() {
    return transitionToIdleState;
}

void ColorProfilePrompt::renderUi(Device* PDN) {
    if (!PDN) return;

    int gameType = player->getPendingProfileGame();
    const char* gameName = (gameType >= 0) ? getGameDisplayName(static_cast<GameType>(gameType)) : "UNKNOWN";
    int current = player->getEquippedColorProfile();
    bool isHunter = player->isHunter();

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("NEW PALETTE!", 15, 12);
    PDN->getDisplay()->drawText(gameName, 15, 28);

    if (current < 0) {
        // First profile -- simple equip prompt
        PDN->getDisplay()->drawText("EQUIP?", 10, 44);
    } else {
        // Has existing -- show current and ask to swap
        char currentStr[32];
        snprintf(currentStr, sizeof(currentStr), "NOW:%s",
                 getColorProfileName(current, isHunter));
        PDN->getDisplay()->drawText(currentStr, 5, 38);
        PDN->getDisplay()->drawText("SWAP?", 10, 48);
    }

    int yesNoY = (current < 0) ? 44 : 48;
    if (selectYes) {
        PDN->getDisplay()->drawText("[YES] NO", 60, yesNoY);
    } else {
        PDN->getDisplay()->drawText("YES [NO]", 60, yesNoY);
    }

    PDN->getDisplay()->drawText("UP:toggle DOWN:ok", 5, 60);
    PDN->getDisplay()->render();
}
