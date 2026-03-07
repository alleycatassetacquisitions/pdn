#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include "state/connect-state.hpp"

Idle::Idle(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, QuickdrawWirelessManager* quickdrawWirelessManager) : ConnectState(remoteDeviceCoordinator, IDLE) {
    this->matchManager = matchManager;
    this->player = player;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
}

Idle::~Idle() {
    player = nullptr;
    matchManager = nullptr;
    quickdrawWirelessManager = nullptr;
}

void Idle::onStateMounted(Device *PDN) {

    // Switch to ESP-NOW mode for peer-to-peer communication
    PDN->getWirelessManager()->enablePeerCommsMode();

    quickdrawWirelessManager->clearCallbacks();
    matchManager->clearCurrentMatch();
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
        idle->displayIsDirty = true;
    };

    PDN->getPrimaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);

    displayIsDirty = true;
}

void Idle::onStateLoop(Device *PDN) {
    if(displayIsDirty) {
        cycleStats(PDN);
        displayIsDirty = false;
    }
}

void Idle::onStateDismounted(Device *PDN) {
    statsIndex = 0;
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getSerialManager()->flushSerial();
    PDN->getSerialManager()->clearCallbacks();  // Clear serial callbacks
}

bool Idle::transitionToDuelCountdown() {
    return isConnected();
}

void Idle::cycleStats(Device *PDN) {
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
    }

    PDN->getDisplay()->render();

    statsIndex++;
    if(statsIndex > statsCount) {
        statsIndex = 0;
    }
}

bool Idle::isPrimaryRequired() {
    return player->isHunter();
}

bool Idle::isAuxRequired() {
    return !player->isHunter();
}
