#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/color-profiles.hpp"
#include "game/progress-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"

static const char* TAG = "ColorProfilePicker";

ColorProfilePicker::ColorProfilePicker(Player* player, ProgressManager* progressManager) :
    State(COLOR_PROFILE_PICKER),
    player(player),
    progressManager(progressManager)
{
}

ColorProfilePicker::~ColorProfilePicker() {
    player = nullptr;
    progressManager = nullptr;
}

void ColorProfilePicker::buildProfileList() {
    profileList.clear();
    // Add eligible game profiles
    for (int gameType : player->getColorProfileEligibility()) {
        profileList.push_back(gameType);
    }
    // Always add DEFAULT as last option
    profileList.push_back(-1);
}

void ColorProfilePicker::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    displayIsDirty = false;
    ledPreviewRequested = false;
    cursorIndex = 0;

    buildProfileList();

    // Pre-select currently equipped profile
    int equipped = player->getEquippedColorProfile();
    for (size_t i = 0; i < profileList.size(); i++) {
        if (profileList[i] == equipped) {
            cursorIndex = static_cast<int>(i);
            break;
        }
    }

    LOG_I(TAG, "Color picker opened, %zu profiles available", profileList.size());

    // Primary (UP) = cycle, Secondary (DOWN) = equip
    parameterizedCallbackFunction equipCb = [](void* ctx) {
        auto* self = static_cast<ColorProfilePicker*>(ctx);
        int selected = self->profileList[self->cursorIndex];
        self->player->setEquippedColorProfile(selected);
        LOG_I(TAG, "Equipped profile: %s",
               getColorProfileName(selected, self->player->isHunter()));
        if (self->progressManager) {
            self->progressManager->saveProgress();
        }
        self->transitionToIdleState = true;
    };

    parameterizedCallbackFunction cycleCb = [](void* ctx) {
        auto* self = static_cast<ColorProfilePicker*>(ctx);
        self->cursorIndex = (self->cursorIndex + 1) % static_cast<int>(self->profileList.size());
        self->displayIsDirty = true;
        self->ledPreviewRequested = true;
    };

    PDN->getPrimaryButton()->setButtonPress(cycleCb, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(equipCb, this, ButtonInteraction::CLICK);

    renderUi(PDN);
}

void ColorProfilePicker::onStateLoop(Device* PDN) {
    // Handle LED preview
    if (ledPreviewRequested) {
        int selected = profileList[cursorIndex];
        if (selected >= 0) {
            // Preview game-specific palette
            const LEDState& preview = getColorProfileState(selected);
            AnimationConfig config;
            config.type = AnimationType::IDLE;
            config.speed = 16;
            config.curve = EaseCurve::LINEAR;
            config.initialState = preview;
            config.loopDelayMs = 0;
            config.loop = true;
            PDN->getLightManager()->startAnimation(config);
        } else {
            // DEFAULT: show role-based idle LED
            AnimationConfig config;
            if (player->isHunter()) {
                config.type = AnimationType::IDLE;
                config.speed = 16;
                config.curve = EaseCurve::LINEAR;
                config.initialState = HUNTER_IDLE_STATE_ALTERNATE;
                config.loopDelayMs = 0;
                config.loop = true;
            } else {
                config.type = AnimationType::VERTICAL_CHASE;
                config.speed = 5;
                config.curve = EaseCurve::ELASTIC;
                config.initialState = BOUNTY_IDLE_STATE;
                config.loopDelayMs = 1500;
                config.loop = true;
            }
            PDN->getLightManager()->startAnimation(config);
        }
        ledPreviewRequested = false;
    }

    if (displayIsDirty) {
        renderUi(PDN);
        displayIsDirty = false;
    }
}

void ColorProfilePicker::onStateDismounted(Device* PDN) {
    profileList.clear();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool ColorProfilePicker::transitionToIdle() {
    return transitionToIdleState;
}

void ColorProfilePicker::renderUi(Device* PDN) {
    if (!PDN) return;

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    // Header region
    PDN->getDisplay()->drawText("COLOR PALETTE", 4, 10);
    PDN->getDisplay()->drawBox(0, 14, 128, 1);  // Header separator

    // Scrolling window: show 3 items centered on cursor
    int listSize = static_cast<int>(profileList.size());
    int visibleCount = (listSize < 3) ? listSize : 3;

    // Calculate start index to keep cursor centered
    int startIdx = cursorIndex;
    if (listSize > 3) {
        // Center cursor in 3-item window
        startIdx = cursorIndex - 1;
        if (startIdx < 0) startIdx = 0;
        if (startIdx + 3 > listSize) startIdx = listSize - 3;
    }

    // Render visible items
    for (int i = 0; i < visibleCount; i++) {
        int idx = startIdx + i;
        int itemY = 24 + (i * 12);
        const char* name = getColorProfileName(profileList[idx], player->isHunter());

        if (idx == cursorIndex) {
            // Inverted highlight for selected item
            PDN->getDisplay()->drawBox(0, itemY - 2, 128, 12);  // White background
            PDN->getDisplay()->setDrawColor(0);  // Black text
            PDN->getDisplay()->drawText(name, 8, itemY);
            PDN->getDisplay()->setDrawColor(1);  // Restore white
        } else {
            // Regular text for unselected items
            PDN->getDisplay()->drawText(name, 8, itemY);
        }
    }

    // Footer separator and controls
    PDN->getDisplay()->drawBox(0, 54, 128, 1);
    PDN->getDisplay()->drawText("[UP] CYCLE  [DOWN] SELECT", 4, 62);

    PDN->getDisplay()->render();
}
