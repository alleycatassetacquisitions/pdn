#include "apps/bonk-it/bonk-it-states.hpp"
#include "device/drivers/logger.hpp"
#include "utils/simple-timer.hpp"

namespace {
static const char* TAG = "GameState";
}  // namespace

GameState::GameState(ControllerWirelessManager* controllerWirelessManager,
                     int* primaryScore)
    : TypedState<FDN>(BonkItStateId::GAME)
    , controllerWirelessManager_(controllerWirelessManager)
    , primaryScore_(primaryScore)
    , session_(controllerWirelessManager) {}

GameState::~GameState() {}

void GameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    phase_                  = Phase::READY;
    roundIndex_             = 0;
    transitionToScoringState_ = false;
    pendingButtonPress_     = false;

    if (primaryScore_) {
        *primaryScore_ = 0;
    }

    session_.reset();
    session_.clearAnimations(fdn);
    session_.renderReadyMessage(fdn);
    readyTimer_.setTimer(kReadyDurationMs);

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&GameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&GameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);
}

void GameState::onStateLoop(FDN* fdn) {
    session_.update(fdn);

    if (pendingButtonPress_ && phase_ == Phase::PLAYING) {
        pendingButtonPress_ = false;
        handleRoundResult(fdn, session_.onButtonPress(pendingButtonId_));
    }

    switch (phase_) {
        case Phase::READY:
            session_.renderReadyMessage(fdn);
            readyTimer_.updateTime();
            if (readyTimer_.expired()) {
                startPlaying(fdn);
            }
            break;

        case Phase::PLAYING: {
            BonkRoundResult result = session_.pollResult();
            if (result != BonkRoundResult::NONE) {
                handleRoundResult(fdn, result);
            }
            break;
        }

        case Phase::LOSS:
            session_.renderLossMessage(fdn);
            if (session_.lossFeedbackComplete() && session_.loseAnimationComplete()) {
                finalizeScores();
                transitionToScoringState_ = true;
            }
            break;
    }
}

void GameState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted — score=%d", primaryScore_ ? *primaryScore_ : 0);
    readyTimer_.invalidate();
    session_.clearAnimations(fdn);
    controllerWirelessManager_->clearCallback();
}

bool GameState::transitionToScoring() {
    return transitionToScoringState_;
}

void GameState::startPlaying(FDN* fdn) {
    phase_ = Phase::PLAYING;
    readyTimer_.invalidate();
    session_.setFadeLightsSpeed(fdn, BonkItSession::fadeSpeedForRound(roundIndex_));
    startNextRound(fdn);
}

void GameState::startNextRound(FDN* fdn) {
    session_.beginCue(
        fdn,
        BonkItSession::randomCue(),
        BonkItSession::durationForRound(roundIndex_));
}

void GameState::handleRoundResult(FDN* fdn, BonkRoundResult result) {
    if (result == BonkRoundResult::NONE) {
        return;
    }

    if (result == BonkRoundResult::CORRECT) {
        session_.pulseCorrectFeedback();
        roundIndex_++;
        session_.setFadeLightsSpeed(fdn, BonkItSession::fadeSpeedForRound(roundIndex_));
        startNextRound(fdn);
        return;
    }

    phase_ = Phase::LOSS;
    session_.stopFadeLights(fdn);
    session_.beginLossFeedback();
    session_.showLoseAnimation(fdn);
}

void GameState::finalizeScores() {
    if (primaryScore_) {
        *primaryScore_ = roundIndex_;
    }
}

void GameState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }

    controllerWirelessManager_->setMacPeer(command.wifiMacAddr);

    if (command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    pendingButtonPress_ = true;
    pendingButtonId_    = command.buttonId;
}
