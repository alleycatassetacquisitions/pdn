#include "game/quickdraw-states.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/idle-animation.hpp"
#include "device/animation/vertical-chase-animation.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include <cstring>

#define TAG "SupporterReady"

SupporterReady::SupporterReady(const GameContext& ctx)
    : ConnectState<PDN>(ctx.remoteDeviceCoordinator, SUPPORTER_READY) {
    this->player = ctx.player;
    this->chainDuelManager = ctx.chainDuelManager;
}

SupporterReady::~SupporterReady() {
    player = nullptr;
}

void SupporterReady::startLEDs(PDN* pdn, bool armed, bool confirmed) {
    AnimationBase* animation;
    AnimationConfig config;
    if (confirmed) {
        animation = new IdleAnimation();
        config.speed = 20;
        config.curve = EaseCurve::LINEAR;
        config.initialState = player->isHunter() ? HUNTER_IDLE_STATE_ALTERNATE : BOUNTY_IDLE_STATE_ALTERNATE;
        config.loop = true;
        config.loopDelayMs = 0;
    } else if (armed) {
        animation = new VerticalChaseAnimation();
        config.speed = 12;
        config.curve = EaseCurve::EASE_IN_OUT;
        config.initialState = player->isHunter() ? HUNTER_IDLE_STATE_ALTERNATE : BOUNTY_IDLE_STATE_ALTERNATE;
        config.loop = true;
        config.loopDelayMs = 0;
    } else {
        animation = new VerticalChaseAnimation();
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
    pdn->getLightManager()->startAnimation(animation, config);
}

void SupporterReady::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "SupporterReady mounted");
    transitionToIdleFlag = false;
    buttonArmed = false;
    hasConfirmed = false;
    displayIsDirty = true;
    ledsAreDirty = true;
    lastResult = 0;
    resultClearTimer.invalidate();

    auto onSupporterPress = [](void *ctx) {
        auto* self = static_cast<SupporterReady*>(ctx);
        if (!self || !self->buttonArmed || self->hasConfirmed) return;
        if (self->chainDuelManager == nullptr) return;

        self->chainDuelManager->sendConfirm();

        self->hasConfirmed = true;
        self->displayIsDirty = true;
        self->ledsAreDirty = true;
    };
    pdn->getPrimaryButton()->setButtonPress(onSupporterPress, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(onSupporterPress, this, ButtonInteraction::CLICK);

    if (chainDuelManager != nullptr) {
        chainDuelManager->setGameEventObserver([this](ChainGameEventType eventType) {
            onChainGameEventReceived(eventType);
        });
    }
}

void SupporterReady::onStateLoop(PDN* pdn) {
    if (chainDuelManager == nullptr || !chainDuelManager->isSupporter()) {
        transitionToIdleFlag = true;
    }

    // Clear result + hasConfirmed 8s after WIN/LOSS. Leaving hasConfirmed
    // set would land pressed supporters on "Locked In" instead of "Ready".
    if (lastResult != 0 && resultClearTimer.expired()) {
        lastResult = 0;
        hasConfirmed = false;
        resultClearTimer.invalidate();
        displayIsDirty = true;
        ledsAreDirty = true;
    }

    if (ledsAreDirty) {
        startLEDs(pdn, buttonArmed, hasConfirmed);
        ledsAreDirty = false;
    }

    if (displayIsDirty) {
        if (lastResult != 0) {
            pdn->getDisplay()->invalidateScreen()
                ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE));
            pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                ->drawText(hasConfirmed ? "Posse" : "Missed", 70, 20);
            pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
                ->drawText(lastResult > 0 ? "Won" : "Lost", 70, 40);
        } else {
            pdn->getDisplay()->invalidateScreen()
                ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE));
            if (hasConfirmed) {
                pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Locked", 74, 24);
                pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText("In", 88, 42);
            } else {
                pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Posse", 70, 20);
                pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(buttonArmed ? "PRESS" : "Ready", 68, 40);
            }
        }
        pdn->getDisplay()->render();
        displayIsDirty = false;
    }
}

void SupporterReady::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "SupporterReady dismounted");
    if (chainDuelManager != nullptr) {
        chainDuelManager->setGameEventObserver(nullptr);
    }
    transitionToIdleFlag = false;
    resultClearTimer.invalidate();
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getLightManager()->stopAnimation();
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
}

void SupporterReady::onChainGameEventReceived(ChainGameEventType eventType) {
    buttonArmed = (eventType == ChainGameEventType::COUNTDOWN);
    displayIsDirty = true;
    ledsAreDirty = true;
    if (eventType == ChainGameEventType::COUNTDOWN) {
        hasConfirmed = false;
        lastResult = 0;
        resultClearTimer.invalidate();
    } else if (eventType == ChainGameEventType::WIN || eventType == ChainGameEventType::LOSS) {
        lastResult = (eventType == ChainGameEventType::WIN) ? 1 : -1;
        resultClearTimer.setTimer(RESULT_DISPLAY_MS);
    }
}

bool SupporterReady::transitionToIdle() {
    return transitionToIdleFlag;
}

bool SupporterReady::isPrimaryRequired() { return false; }
bool SupporterReady::isAuxRequired() { return true; }
