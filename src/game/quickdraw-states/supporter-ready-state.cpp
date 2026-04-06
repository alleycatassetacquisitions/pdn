#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/jack-roles.hpp"
#include "device/device.hpp"
#include "wireless/team-packet.hpp"
#include <string>

SupporterReady::SupporterReady(Player* player, RemoteDeviceCoordinator* rdc, Quickdraw* quickdraw)
    : ConnectState(rdc, SUPPORTER_READY), player_(player), quickdraw_(quickdraw) {}

SupporterReady::~SupporterReady() = default;

void SupporterReady::onButtonPressed(void* ctx) {
    auto* state = static_cast<SupporterReady*>(ctx);
    if (!state->duelActive_ || state->hasConfirmed_) return;
    state->hasConfirmed_ = true;
    sendTeamPacket(state->peerComms_, state->championMac_, TeamCommandType::CONFIRM, state->position_);
    state->renderDisplay("Sent!");
}

void SupporterReady::renderDisplay(const char* status) {
    if (!device_) return;
    device_->getDisplay()->invalidateScreen();
    device_->getDisplay()->drawImage(getImageForAllegiance(player_->getAllegiance(), ImageType::IDLE));
    device_->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText(championName_[0] ? championName_ : "Support", 68, 20);
    device_->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText(status, 68, 40);
    device_->getDisplay()->render();
}

void SupporterReady::onStateMounted(Device* PDN) {
    device_ = PDN;
    peerComms_ = PDN->getPeerComms();
    hasConfirmed_ = false;
    duelActive_ = false;
    transitionToWin_ = false;
    transitionToLose_ = false;
    transitionToIdle_ = false;

    if (quickdraw_) {
        quickdraw_->setGameEventCallback([this](GameEventType evt) {
            handleGameEvent(evt);
        });
    }

    AnimationConfig config;
    if(player_->isHunter()) {
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

    PDN->getPrimaryButton()->setButtonPress(onButtonPressed, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(onButtonPressed, this, ButtonInteraction::CLICK);

    downstreamInvited_ = false;
    registered_ = false;
    retryTimer_.setTimer(RETRY_MS);

    renderDisplay("Ready");
    safetyTimeout_.setTimer(SAFETY_TIMEOUT_MS);
}

void SupporterReady::onStateLoop(Device* PDN) {
    (void)PDN;

    if (safetyTimeout_.expired()) {
        sendTeamPacket(peerComms_, championMac_, TeamCommandType::DEREGISTER, position_);
        transitionToIdle_ = true;
        return;
    }

    retryTimer_.updateTime();
    bool shouldRetry = retryTimer_.expired();

    // Send/retry REGISTER to champion
    if (!registered_ || shouldRetry) {
        sendTeamPacket(peerComms_, championMac_, TeamCommandType::REGISTER, position_);
        registered_ = true;
    }

    // Forward/retry invite to downstream peer
    SerialIdentifier downstreamJack = supporterJack(player_);
    if (remoteDeviceCoordinator->getPortStatus(downstreamJack) == PortStatus::CONNECTED) {
        if (!downstreamInvited_ || shouldRetry) {
            const uint8_t* downstreamMac = remoteDeviceCoordinator->getPeerMac(downstreamJack);
            if (downstreamMac && memcmp(downstreamMac, championMac_, 6) != 0) {
                sendRegisterInvite(peerComms_, downstreamMac, position_, championIsHunter_,
                                   championMac_, championName_);
                downstreamInvited_ = true;
            }
        }
    }

    if (shouldRetry) retryTimer_.setTimer(RETRY_MS);

    if (quickdraw_ && quickdraw_->hasTeamInvite()) {
        if (memcmp(quickdraw_->getInviteChampionMac(), championMac_, 6) != 0) {
            // Chain merge: different champion, re-register under the new one
            sendTeamPacket(peerComms_, championMac_, TeamCommandType::DEREGISTER, position_);
            transitionToIdle_ = true;
            return;
        }
        // Same champion re-invited: new duel cycle, reset supporter state
        hasConfirmed_ = false;
        duelActive_ = false;
        registered_ = false;
        renderDisplay("Ready");
    }

    SerialIdentifier championJack = opponentJack(player_);
    if (!transitionToIdle_ && remoteDeviceCoordinator->getPortStatus(championJack) == PortStatus::DISCONNECTED) {
        sendTeamPacket(peerComms_, championMac_, TeamCommandType::DEREGISTER, position_);
        transitionToIdle_ = true;
    }
}

void SupporterReady::handleGameEvent(GameEventType evt) {
    switch (evt) {
        case GameEventType::COUNTDOWN:
        case GameEventType::DRAW:
            duelActive_ = true;
            if (!hasConfirmed_) renderDisplay("Press!");
            break;
        case GameEventType::WIN:
            renderDisplay("WIN!");
            transitionToWin_ = true;
            break;
        case GameEventType::LOSS:
            renderDisplay("LOSS");
            transitionToLose_ = true;
            break;
    }
}

void SupporterReady::onStateDismounted(Device* PDN) {
    if (quickdraw_) quickdraw_->clearGameEventCallback();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    retryTimer_.invalidate();
    safetyTimeout_.invalidate();
    PDN->getPeerComms()->removePeer(const_cast<uint8_t*>(championMac_));
    device_ = nullptr;
}

bool SupporterReady::transitionToWin() { return transitionToWin_; }
bool SupporterReady::transitionToLose() { return transitionToLose_; }
bool SupporterReady::transitionToIdle() { return transitionToIdle_; }
bool SupporterReady::isPrimaryRequired() { return false; }
bool SupporterReady::isAuxRequired() { return false; }
