#include "apps/quickdraw-demo/quickdraw-demo-states.hpp"
#include "game/quickdraw-countdown.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "GameState";
}

GameState::GameState(ControllerWirelessManager* controllerWirelessManager,
                     int* primaryScore,
                     int* secondaryScore)
    : TypedState<FDN>(QuickdrawDemoStateId::GAME)
    , controllerWirelessManager_(controllerWirelessManager)
    , primaryScore_(primaryScore)
    , secondaryScore_(secondaryScore)
    , session_(controllerWirelessManager) {}

GameState::~GameState() {}

void GameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    winCount_ = 0;
    reactionTimeTotalMs_ = 0;
    reactionSampleCount_ = 0;
    showingOutcome_ = false;
    showingRoundResult_ = false;
    transitionToScoringState_ = false;
    lastOutcome_ = QuickdrawPracticeOutcome::NONE;
    lastReactionMs_ = 0;

    if (primaryScore_) {
        *primaryScore_ = 0;
    }
    if (secondaryScore_) {
        *secondaryScore_ = 0;
    }

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&GameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&GameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);

    startNextMatch(fdn);
}

void GameState::onStateLoop(FDN* fdn) {
    if (showingRoundResult_) {
        roundResultTimer_.updateTime();
        session_.renderRoundResult(fdn, lastOutcome_, lastReactionMs_);

        if (roundResultTimer_.expired()) {
            showingRoundResult_ = false;
            roundResultTimer_.invalidate();

            if (lastOutcome_ == QuickdrawPracticeOutcome::WIN) {
                winCount_++;
                startNextMatch(fdn);
                return;
            }

            finalizeScores();
            transitionToScoringState_ = true;
        }
        return;
    }

    session_.update(fdn);

    if (session_.phase() == QuickdrawPracticePhase::READY) {
        session_.renderReady(fdn, true);
    } else if (session_.phase() == QuickdrawPracticePhase::COUNTDOWN) {
        session_.renderCountdown(fdn);
    } else if (session_.phase() == QuickdrawPracticePhase::DRAW) {
        session_.renderDraw(fdn);
    }

    if (session_.matchResolved() && !showingOutcome_) {
        handleMatchOutcome(fdn);
        return;
    }

    if (showingOutcome_ && session_.outcomeTimerExpired()) {
        showingOutcome_ = false;
        lastOutcome_ = session_.outcome();
        lastReactionMs_ = session_.playerReactionMs();
        session_.clearAnimations(fdn);
        session_.clearOutcome();

        showingRoundResult_ = true;
        roundResultTimer_.setTimer(kRoundResultDisplayMs);
        session_.renderRoundResult(fdn, lastOutcome_, lastReactionMs_);
    }
}

void GameState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted — wins=%d avgMs=%d",
          winCount_,
          secondaryScore_ ? *secondaryScore_ : 0);
    roundResultTimer_.invalidate();
    session_.clearAnimations(fdn);
    controllerWirelessManager_->clearCallback();
}

bool GameState::transitionToScoring() {
    return transitionToScoringState_;
}

void GameState::startNextMatch(FDN* fdn) {
    session_.resetMatch();
    session_.setOpponentEnabled(true);
    session_.setOpponentResponseMs(QuickdrawCountdown::opponentResponseMsForMatch(winCount_));
    session_.beginReady();
    session_.clearAnimations(fdn);
    session_.renderReady(fdn, true);
}

void GameState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }

    controllerWirelessManager_->setMacPeer(command.wifiMacAddr);

    if (command.buttonId != ButtonIdentifier::SECONDARY_BUTTON ||
        command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    session_.onPlayerShot(session_.phase() == QuickdrawPracticePhase::DRAW);
}

void GameState::handleMatchOutcome(FDN* fdn) {
    showingOutcome_ = true;

    if (session_.playerReactionMs() > 0) {
        reactionTimeTotalMs_ += session_.playerReactionMs();
        reactionSampleCount_++;
    }

    if (session_.outcome() == QuickdrawPracticeOutcome::WIN) {
        session_.showWinAnimation(fdn);
        return;
    }

    session_.showLoseAnimation(fdn);
}

void GameState::finalizeScores() {
    if (primaryScore_) {
        *primaryScore_ = winCount_;
    }

    if (secondaryScore_) {
        if (reactionSampleCount_ > 0) {
            *secondaryScore_ = static_cast<int>(reactionTimeTotalMs_ / reactionSampleCount_);
        } else {
            *secondaryScore_ = 0;
        }
    }
}
