#include "device/drivers/serial-wrapper.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "game/posse-manager.hpp"
#include "game/shootout-manager.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/animation/idle-animation.hpp"
#include "device/animation/vertical-chase-animation.hpp"
#include "wireless/mac-functions.hpp"
#include "state/connect-state.hpp"
#include <cstring>

Idle::Idle(const GameContext& ctx)
    : ConnectState<PDN>(ctx.remoteDeviceCoordinator, IDLE)
    , player(ctx.player)
    , matchManager(ctx.matchManager)
    , posseManager(ctx.posseManager)
    , shootoutManager(ctx.shootoutManager) {}

Idle::~Idle() {
    player = nullptr;
    matchManager = nullptr;
    posseManager = nullptr;
    shootoutManager = nullptr;
}

void Idle::onStateMounted(PDN* pdn) {
    pdn->getWirelessManager()->enablePeerCommsMode();

    AnimationConfig config;

    AnimationBase* animation;
    if(player->isHunter()) {
        animation = new IdleAnimation();
        config.speed = 16;
        config.curve = EaseCurve::LINEAR;
        config.initialState = HUNTER_IDLE_STATE_ALTERNATE;
        config.loopDelayMs = 0;
        config.loop = true;
    } else {
        animation = new VerticalChaseAnimation();
        config.speed = 5;
        config.curve = EaseCurve::ELASTIC;
        config.initialState = BOUNTY_IDLE_STATE;
        config.loopDelayMs = 1500;
        config.loop = true;
    }
    pdn->getLightManager()->startAnimation(animation, config);

    parameterizedCallbackFunction cycleStats = [](void *ctx) {
        Idle* idle = (Idle*)ctx;
        idle->statsIndex++;
        if (idle->statsIndex > idle->statsCount) {
            idle->statsIndex = 0;
        }
        idle->displayIsDirty = true;
    };

    pdn->getPrimaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);

    displayIsDirty = true;
}

void Idle::onStateLoop(PDN* pdn) {
    if(displayIsDirty) {
        renderStats(pdn);
        displayIsDirty = false;
    }

    bool shootoutActive = shootoutManager && shootoutManager->active();
    if (!shootoutActive && isConnected()) {
        if (posseManager->canInitiateMatch()) {
            if (!matchInitialized) {
                const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
                if (peerMac != nullptr) {
                    matchManager->initializeMatch(const_cast<uint8_t*>(peerMac));
                    matchInitialized = true;
                    matchInitializationTimer.setTimer(MATCH_INITIALIZATION_TIMEOUT);
                }
            }
        }
    }

    if(matchInitializationTimer.expired()) {
        matchInitialized = false;
        matchManager->clearCurrentMatch();
    }
}

void Idle::onStateDismounted(PDN* pdn) {
    if (!matchManager->isMatchReady() ||
        (shootoutManager && shootoutManager->shouldEnterProposal())) {
        matchManager->clearCurrentMatch();
    }
    statsIndex = 0;
    matchInitializationTimer.invalidate();
    matchInitialized = false;
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
}

bool Idle::transitionToMatchCountdown() { return matchManager->isMatchReady(); }

bool Idle::transitionToSupporterReady() { return posseManager->isSupporter(); }

void Idle::renderStats(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->render();

    if(statsIndex == 0) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Wins",74, 20);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getWins()).c_str(), 88, 40);
    } else if(statsIndex == 1) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Streak",70, 20);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getStreak()).c_str(), 88, 40);
    } else if(statsIndex == 2) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Losses",70, 20);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getLosses()).c_str(), 88, 40);
    } else if(statsIndex == 3) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Matches",70, 20);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getMatchesPlayed()).c_str(), 88, 40);
    } else if(statsIndex == 4) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Last",70, 20)->drawText("Reaction", 70, 35);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getLastReactionTime()).c_str(), 80, 55);
    } else if(statsIndex == 5) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Average",70, 20)->drawText("Reaction", 70, 35);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getAverageReactionTime()).c_str(), 80, 55);
    } else if (statsIndex == 6) {
        size_t sc = posseManager->getSupporterChainPeers().size();
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Posse",70, 20);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(sc).c_str(), 88, 40);
    } else if (statsIndex == 7) {
        int glyph_size = 32;
        pdn->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH)->renderGlyph(player->getSymbol()->getSymbolGlyph(), (int)(64 + (64 - glyph_size)/2), (int)(64 - (64 - glyph_size)/2));
    }

    pdn->getDisplay()->render();
}

bool Idle::isPrimaryRequired() {
    return player->isHunter();
}

bool Idle::isAuxRequired() {
    return !player->isHunter();
}

bool Idle::transitionToSymbol() {
    // An in-flight match init owns the device until its timeout clears, so an
    // FDN cabled during that window routes to symbol match once it does.
    return !matchInitialized &&
           (getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::FDN ||
            getPeerDeviceType(SerialIdentifier::INPUT_JACK) == DeviceType::FDN);
}
