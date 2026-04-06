#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include "wireless/team-packet.hpp"
#include "game/jack-roles.hpp"
#include "state/connect-state.hpp"

Idle::Idle(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
           std::function<int()> getSupporterCount, std::function<void()> clearChainState,
           std::function<bool()> isInviteRejected)
    : ConnectState(remoteDeviceCoordinator, IDLE),
      player(player), matchManager(matchManager),
      getSupporterCount_(std::move(getSupporterCount)),
      clearChainState_(std::move(clearChainState)),
      isInviteRejected_(std::move(isInviteRejected)) {
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
        idle->displayIsDirty = true;
    };

    PDN->getPrimaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);

    displayIsDirty = true;
}

void Idle::onStateLoop(Device *PDN) {
    int supporters = getSupporterCount_();
    if (supporters != lastSupporterCount_) {
        lastSupporterCount_ = supporters;
        statsIndex = 6;
        displayIsDirty = true;
    }

    if(displayIsDirty) {
        cycleStats(PDN);
        displayIsDirty = false;
    }

    SerialIdentifier sJack = supporterJack(player);
    SerialIdentifier oJack = opponentJack(player);

    bool sConnected = remoteDeviceCoordinator->getPortStatus(sJack) == PortStatus::CONNECTED;
    const uint8_t* supporterMac = sConnected ? remoteDeviceCoordinator->getPeerMac(sJack) : nullptr;

    const uint8_t* myMac = PDN->getWirelessManager()->getMacAddress();
    if (supporterMac && !(isInviteRejected_ && isInviteRejected_())) {
        inviteRetryTimer_.updateTime();
        if (!inviteRetryTimer_.isRunning() || inviteRetryTimer_.expired()) {
            sendRegisterInvite(PDN->getPeerComms(), supporterMac, 0, player->isHunter(),
                               myMac, player->getName().c_str());
            inviteRetryTimer_.setTimer(INVITE_RETRY_MS);
        }
    } else if (inviteRetryTimer_.isRunning()) {
        inviteRetryTimer_.invalidate();
        clearChainState_();
    }

    // Only hunters initiate duels
    if (player->isHunter() && isConnected() && !matchInitialized) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(oJack);
        static const uint8_t zeroMac[6] = {};
        if (peerMac != nullptr && memcmp(peerMac, zeroMac, 6) != 0) {
            matchManager->initializeMatch(const_cast<uint8_t*>(peerMac));
            matchInitialized = true;
            matchInitializationTimer.setTimer(MATCH_INITIALIZATION_TIMEOUT);
        }
    }

    if(matchInitializationTimer.expired()) {
        matchInitialized = false;
        matchManager->clearCurrentMatch("idle-timer");
    }
}

void Idle::onStateDismounted(Device *PDN) {
    statsIndex = 0;
    matchInitializationTimer.invalidate();
    matchInitialized = false;
    inviteRetryTimer_.invalidate();
    lastSupporterCount_ = 0;
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool Idle::transitionToDuelCountdown() {
    return matchManager->isMatchReady();
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
    } else if(statsIndex == 6) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Support",70, 20);
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(getSupporterCount_()).c_str(), 88, 40);
    }

    PDN->getDisplay()->render();

    statsIndex = (statsIndex + 1) % 7;
}

bool Idle::isPrimaryRequired() {
    return player->isHunter();
}

bool Idle::isAuxRequired() {
    return !player->isHunter();
}
