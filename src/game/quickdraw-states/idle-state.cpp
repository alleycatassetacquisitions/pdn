#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"

Idle::Idle(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager) : State(IDLE) {
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
    PDN->setOnStringReceivedCallback(std::bind(&Idle::serialEventCallbacks, this, std::placeholders::_1));
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

        heartbeatTimer.setTimer(HEARTBEAT_INTERVAL_MS);
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
    if(!player->isHunter() && heartbeatTimer.expired()) {
        PDN->writeString(SERIAL_HEARTBEAT.c_str());
        heartbeatTimer.setTimer(HEARTBEAT_INTERVAL_MS);
    }

    if(sendMacAddress) {
        uint8_t* macAddr = PDN->getWirelessManager()->getMacAddress();
        const char* macStr = MacToString(macAddr);
        LOG_I("IDLE", "Preparing to Send Mac Address: %s", macStr);
        
        // Send as single concatenated message to avoid fragmentation
        std::string message = SEND_MAC_ADDRESS + std::string(macStr);
        PDN->writeString(message.c_str());
        transitionToHandshakeState = true;
    }

    if(displayIsDirty) {
        cycleStats(PDN);
        displayIsDirty = false;
    }
}

void Idle::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
    sendMacAddress = false;
    waitingForMacAddress = false;
    heartbeatTimer.invalidate();
    statsIndex = 0;
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->flushSerial();
    PDN->clearCallbacks();  // Clear serial callbacks
}

void Idle::serialEventCallbacks(const std::string& message) {
    LOG_I("IDLE", "Serial event received: %s", message.c_str());
    if(message.compare(SERIAL_HEARTBEAT) == 0) {
        sendMacAddress = true;  
    } else if(message.rfind(SEND_MAC_ADDRESS, 0) == 0) {
        // Message starts with "smac" - extract MAC address after prefix
        std::string macAddress = message.substr(SEND_MAC_ADDRESS.length());
        LOG_I("IDLE", "Received opponent MAC address: %s", macAddress.c_str());
        player->setOpponentMacAddress(macAddress);
        transitionToHandshakeState = true;
    }
}

bool Idle::transitionToHandshake() {
    return transitionToHandshakeState;
}

void Idle::cycleStats(Device *PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->render();

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
