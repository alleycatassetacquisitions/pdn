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

    // Primary = scroll, Secondary = confirm
    parameterizedCallbackFunction scrollCb = [](void* ctx) {
        auto* self = static_cast<ColorProfilePicker*>(ctx);
        self->cursorIndex = (self->cursorIndex + 1) % static_cast<int>(self->profileList.size());
        self->displayIsDirty = true;
    };

    parameterizedCallbackFunction confirmCb = [](void* ctx) {
        auto* self = static_cast<ColorProfilePicker*>(ctx);
        int selected = self->profileList[self->cursorIndex];
        self->player->setEquippedColorProfile(selected);
        LOG_I(TAG, "Equipped profile: %s", getColorProfileName(selected));
        if (self->progressManager) {
            self->progressManager->saveProgress();
        }
        self->transitionToIdleState = true;
    };

    PDN->getPrimaryButton()->setButtonPress(scrollCb, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);

    renderUi(PDN);
}

void ColorProfilePicker::onStateLoop(Device* PDN) {
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
    PDN->getDisplay()->drawText("COLOR PALETTE", 10, 12);

    // Show up to 3 items centered around cursor
    int listSize = static_cast<int>(profileList.size());
    int startIdx = cursorIndex;  // Start from cursor, show up to 3
    int visibleCount = (listSize < 3) ? listSize : 3;

    for (int i = 0; i < visibleCount; i++) {
        int idx = (startIdx + i) % listSize;
        int y = 28 + (i * 12);
        const char* name = getColorProfileName(profileList[idx]);

        if (idx == cursorIndex) {
            char line[32];
            snprintf(line, sizeof(line), "> %s", name);
            PDN->getDisplay()->drawText(line, 5, y);
        } else {
            char line[32];
            snprintf(line, sizeof(line), "  %s", name);
            PDN->getDisplay()->drawText(line, 5, y);
        }
    }

    PDN->getDisplay()->drawText("UP:scroll DOWN:ok", 5, 60);
    PDN->getDisplay()->render();
}
