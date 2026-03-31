#include "device/drivers/serial-wrapper.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "game/chain-duel-manager.hpp"
#include "device/drivers/logger.hpp"
#include "symbol-match/symbol-manager.hpp"
#include "symbol-match/symbol-match.hpp"
#include "wireless/mac-functions.hpp"
#include "state/connect-state.hpp"
#include <cstring>

Idle::Idle(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager) : ConnectState(remoteDeviceCoordinator, IDLE) {
    this->matchManager = matchManager;
    this->player = player;
    this->chainDuelManager = chainDuelManager;
}

Idle::~Idle() {
    player = nullptr;
    matchManager = nullptr;
}

void Idle::onStateMounted(Device *PDN) {

    // Switch to ESP-NOW mode for peer-to-peer communication
    PDN->getWirelessManager()->enablePeerCommsMode();

    AnimationConfig config;
    
    if(player->isHunter()) {
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

    parameterizedCallbackFunction cycleStats = [](void *ctx) {
        Idle* idle = (Idle*)ctx;
        idle->statsIndex++;
        if (idle->statsIndex > idle->statsCount) {
            idle->statsIndex = 0;
        }
        idle->displayIsDirty = true;
    };

    PDN->getPrimaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);

    displayIsDirty = true;
}

void Idle::onStateLoop(Device *PDN) {
    if(displayIsDirty) {
        renderStats(PDN);
        displayIsDirty = false;
    }

    if (chainDuelManager->canInitiateMatch() && isConnected() && getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::PDN) {
        if (!matchInitialized) {
            const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
            if (peerMac != nullptr) {
                matchManager->initializeMatch(const_cast<uint8_t*>(peerMac));
                matchInitialized = true;
                matchInitializationTimer.setTimer(MATCH_INITIALIZATION_TIMEOUT);
            }
        }
    }

    else if (getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::FDN) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
        if (peerMac != nullptr) {
            // initiate symbol match

        }
    }


    if(matchInitializationTimer.expired()) {
        matchInitialized = false;
        matchManager->clearCurrentMatch();
    }
}

void Idle::onStateDismounted(Device *PDN) {
    statsIndex = 0;
    matchInitializationTimer.invalidate();
    matchInitialized = false;
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool Idle::transitionToDuelCountdown() {
    return matchManager->isMatchReady();
}

bool Idle::transitionToSupporterReady() {
    // A supporter is a device whose opponent-jack peer is same role.
    // This means the chain extends upstream toward the champion.
    return chainDuelManager->isSupporter();
}

void Idle::renderStats(Device *PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->render();

    if(statsIndex == 0) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Wins",74, 20);
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getWins()).c_str(), 88, 40);
    } else if(statsIndex == 1) {        
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Streak",70, 20);
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getStreak()).c_str(), 88, 40);
    } else if(statsIndex == 2) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Losses",70, 20);
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getLosses()).c_str(), 88, 40);
    } else if(statsIndex == 3) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Matches",70, 20);
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getMatchesPlayed()).c_str(), 88, 40);
    } else if(statsIndex == 4) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Last",70, 20)->drawText("Reaction", 70, 35);
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getLastReactionTime()).c_str(), 80, 55);
    } else if(statsIndex == 5) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Average",70, 20)->drawText("Reaction", 70, 35);
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getAverageReactionTime()).c_str(), 80, 55);
    } else if(statsIndex == 6) {
        int glyph_size = 32;
        PDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH)->renderGlyph(player->getSymbol()->getSymbolGlyph(), (int)(64 + (64 - glyph_size)/2), (int)(64 - (64 - glyph_size)/2));
    }

    PDN->getDisplay()->render();
}

bool Idle::isPrimaryRequired() {
    return player->isHunter();
}

bool Idle::isAuxRequired() {
    return !player->isHunter();
}
