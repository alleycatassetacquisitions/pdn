#include "game/quickdraw-states.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include <cstring>

#define TAG "SupporterReady"

SupporterReady::SupporterReady(Player *player, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager)
    : ConnectState(remoteDeviceCoordinator, SUPPORTER_READY) {
    this->player = player;
    this->chainDuelManager = chainDuelManager;
}

SupporterReady::~SupporterReady() {
    player = nullptr;
}

void SupporterReady::startLEDs(Device *PDN, bool armed, bool confirmed) {
    AnimationConfig config;
    if (confirmed) {
        config.type = AnimationType::IDLE;
        config.speed = 20;
        config.curve = EaseCurve::LINEAR;
        config.initialState = player->isHunter() ? HUNTER_IDLE_STATE_ALTERNATE : BOUNTY_IDLE_STATE_ALTERNATE;
        config.loop = true;
        config.loopDelayMs = 0;
    } else if (armed) {
        config.type = AnimationType::VERTICAL_CHASE;
        config.speed = 12;
        config.curve = EaseCurve::EASE_IN_OUT;
        config.initialState = player->isHunter() ? HUNTER_IDLE_STATE_ALTERNATE : BOUNTY_IDLE_STATE_ALTERNATE;
        config.loop = true;
        config.loopDelayMs = 0;
    } else {
        config.type = AnimationType::VERTICAL_CHASE;
        config.speed = 3;
        config.curve = EaseCurve::EASE_OUT;
        config.loop = true;
        config.loopDelayMs = 2000;
        LEDState state;
        const LEDColor* colors = player->isHunter() ? hunterIdleLEDColorsAlternate : bountyIdleLEDColorsAlternate;
        for (int i = 0; i < 9; i++) {
            state.leftLights[i] = state.rightLights[i] = LEDState::SingleLEDState(colors[i], 40);
        }
        config.initialState = state;
    }
    PDN->getLightManager()->startAnimation(config);
}

void SupporterReady::onStateMounted(Device *PDN) {
    LOG_W(TAG, "SupporterReady mounted");
    transitionToIdleFlag = false;
    buttonArmed = false;
    hasConfirmed = false;
    displayIsDirty = true;
    ledsAreDirty = true;
    lastResult = 0;

    auto onSupporterPress = [](void *ctx) {
        auto* self = static_cast<SupporterReady*>(ctx);
        if (!self || !self->buttonArmed || self->hasConfirmed) return;
        if (self->chainDuelManager == nullptr) return;

        self->chainDuelManager->sendConfirm();

        self->hasConfirmed = true;
        self->displayIsDirty = true;
        self->ledsAreDirty = true;
    };
    PDN->getPrimaryButton()->setButtonPress(onSupporterPress, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(onSupporterPress, this, ButtonInteraction::CLICK);
    cachedPDN = PDN;
}

void SupporterReady::onStateLoop(Device *PDN) {
    if (chainDuelManager == nullptr || !chainDuelManager->isSupporter()) {
        transitionToIdleFlag = true;
    }

    // Detect lastResult transitions driven by the WiFi-task packet callback
    // and update the timer here on the main task. SimpleTimer is not atomic;
    // calling setTimer/invalidate from the packet callback would race with
    // expired() reads below.
    int currentResult = lastResult.load();
    if (currentResult != lastProcessedResult_) {
        if (currentResult != 0) {
            resultClearTimer.setTimer(8000);
        } else {
            resultClearTimer.invalidate();
        }
        lastProcessedResult_ = currentResult;
    }

    // Clear result + hasConfirmed 8s after WIN/LOSS. Leaving hasConfirmed
    // set would land pressed supporters on "Locked In" instead of "Ready".
    if (lastResult != 0 && resultClearTimer.expired()) {
        lastResult = 0;
        hasConfirmed = false;
        displayIsDirty = true;
        ledsAreDirty = true;
    }

    if (ledsAreDirty) {
        startLEDs(PDN, buttonArmed, hasConfirmed);
        ledsAreDirty = false;
    }

    if (displayIsDirty) {
        if (lastResult != 0) {
            PDN->getDisplay()->invalidateScreen()
                ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE));
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                ->drawText(hasConfirmed ? "Posse" : "Missed", 70, 20);
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
                ->drawText(lastResult > 0 ? "Won" : "Lost", 70, 40);
        } else {
            PDN->getDisplay()->invalidateScreen()
                ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE));
            if (hasConfirmed) {
                PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Locked", 74, 24);
                PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText("In", 88, 42);
            } else {
                PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Posse", 70, 20);
                PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(buttonArmed ? "PRESS" : "Ready", 68, 40);
            }
        }
        PDN->getDisplay()->render();
        displayIsDirty = false;
    }
}

void SupporterReady::onStateDismounted(Device *PDN) {
    LOG_W(TAG, "SupporterReady dismounted");
    transitionToIdleFlag = false;
    buttonArmed = false;
    hasConfirmed = false;
    cachedPDN = nullptr;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getLightManager()->stopAnimation();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
}

// Runs on the ESP-NOW WiFi task on hardware. Touch only atomic fields here;
// onStateLoop (main task) observes lastResult transitions and manages the
// non-atomic resultClearTimer.
void SupporterReady::onChainGameEventReceived(uint8_t event_type, const uint8_t* senderMac) {
    (void)senderMac;
    ChainGameEventType et = static_cast<ChainGameEventType>(event_type);
    switch (et) {
        case ChainGameEventType::COUNTDOWN:
            buttonArmed = true;
            hasConfirmed = false;
            lastResult = 0;
            displayIsDirty = true;
            ledsAreDirty = true;
            break;
        case ChainGameEventType::DRAW:
            buttonArmed = false;
            displayIsDirty = true;
            ledsAreDirty = true;
            break;
        case ChainGameEventType::WIN:
            buttonArmed = false;
            lastResult = 1;
            displayIsDirty = true;
            ledsAreDirty = true;
            break;
        case ChainGameEventType::LOSS:
            buttonArmed = false;
            lastResult = -1;
            displayIsDirty = true;
            ledsAreDirty = true;
            break;
    }
}

bool SupporterReady::transitionToIdle() {
    return transitionToIdleFlag;
}

bool SupporterReady::isPrimaryRequired() { return false; }
bool SupporterReady::isAuxRequired() { return true; }
