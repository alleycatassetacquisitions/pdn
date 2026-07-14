#include "apps/quickdraw-demo/quickdraw-demo-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "TutorialState";
}  // namespace

bool TutorialState::isMessagePhase(Phase phase) {
    switch (phase) {
        case Phase::INTRO_1:
        case Phase::INTRO_2:
        case Phase::INTRO_3:
        case Phase::RULE_1:
        case Phase::RULE_2:
        case Phase::RULE_3:
        case Phase::TRY_AGAIN:
        case Phase::WIN_OUTRO_1:
        case Phase::WIN_OUTRO_2:
            return true;
        default:
            return false;
    }
}

bool TutorialState::phaseWaitsForButton(Phase phase) {
    return phase == Phase::INTRO_3;
}

TutorialState::TutorialState(ControllerWirelessManager* controllerWirelessManager)
    : TypedState<FDN>(QuickdrawDemoStateId::TUTORIAL)
    , controllerWirelessManager_(controllerWirelessManager)
    , session_(controllerWirelessManager) {}

TutorialState::~TutorialState() {}

void TutorialState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    phase_ = Phase::INTRO_1;
    showingOutcome_ = false;
    transitionToMainMenuState_ = false;
    session_.resetMatch();
    showCurrentMessage(fdn);
    restartMessageTimer();

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&TutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&TutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);
}

void TutorialState::onStateLoop(FDN* fdn) {
    session_.update(fdn);

    if (pendingBottomButton_) {
        pendingBottomButton_ = false;
        handleBottomButton(fdn);
    }

    if (phase_ == Phase::READY || phase_ == Phase::COUNTDOWN) {
        messageTimer_.invalidate();

        if (phase_ == Phase::READY && session_.phase() == QuickdrawPracticePhase::COUNTDOWN) {
            phase_ = Phase::COUNTDOWN;
        }

        if (phase_ == Phase::COUNTDOWN) {
            if (session_.phase() == QuickdrawPracticePhase::DRAW) {
                session_.renderDraw(fdn);
            } else if (session_.phase() == QuickdrawPracticePhase::COUNTDOWN) {
                session_.renderCountdown(fdn);
            }
        } else {
            session_.renderReady(fdn, false);
        }

        if (session_.matchResolved() && !showingOutcome_) {
            handleMatchOutcome(fdn);
        } else if (showingOutcome_ && session_.outcomeTimerExpired()) {
            showingOutcome_ = false;
            session_.clearAnimations(fdn);

            if (session_.outcome() == QuickdrawPracticeOutcome::EARLY_SHOT ||
                session_.outcome() == QuickdrawPracticeOutcome::TIMEOUT) {
                phase_ = Phase::TRY_AGAIN;
                showCurrentMessage(fdn);
                restartMessageTimer();
            } else if (session_.outcome() == QuickdrawPracticeOutcome::WIN) {
                phase_ = Phase::WIN_OUTRO_1;
                showCurrentMessage(fdn);
                restartMessageTimer();
            }
            session_.clearOutcome();
        }
        return;
    }

    showCurrentMessage(fdn);

    if (isMessagePhase(phase_) && !phaseWaitsForButton(phase_)) {
        messageTimer_.updateTime();
        if (messageTimer_.expired()) {
            advanceMessagePhase(fdn);
        }
    }
}

void TutorialState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    messageTimer_.invalidate();
    session_.clearAnimations(fdn);
    controllerWirelessManager_->clearCallback();
}

bool TutorialState::transitionToMainMenu() {
    return transitionToMainMenuState_;
}

void TutorialState::restartMessageTimer() {
    if (!isMessagePhase(phase_) || phaseWaitsForButton(phase_)) {
        messageTimer_.invalidate();
        return;
    }

    messageTimer_.setTimer(kMessageAutoAdvanceMs);
}

void TutorialState::advanceMessagePhase(FDN* fdn) {
    messageTimer_.invalidate();

    switch (phase_) {
        case Phase::INTRO_1:
            phase_ = Phase::INTRO_2;
            break;
        case Phase::INTRO_2:
            phase_ = Phase::INTRO_3;
            break;
        case Phase::INTRO_3:
            phase_ = Phase::RULE_1;
            break;
        case Phase::RULE_1:
            phase_ = Phase::RULE_2;
            break;
        case Phase::RULE_2:
            phase_ = Phase::RULE_3;
            break;
        case Phase::RULE_3:
            beginReadyCountdown(fdn);
            return;
        case Phase::TRY_AGAIN:
            beginReadyCountdown(fdn);
            return;
        case Phase::WIN_OUTRO_1:
            phase_ = Phase::WIN_OUTRO_2;
            break;
        case Phase::WIN_OUTRO_2:
            transitionToMainMenuState_ = true;
            return;
        default:
            return;
    }

    showCurrentMessage(fdn);
    restartMessageTimer();
}

void TutorialState::showCurrentMessage(FDN* fdn) {
    switch (phase_) {
        case Phase::INTRO_1:
            session_.renderTutorialMessage(
                fdn,
                "So, you wanna",
                "learn how to",
                "quickdraw, huh?");
            break;
        case Phase::INTRO_2:
            session_.renderTutorialMessage(
                fdn,
                "Press the bottom",
                "button on your PDN",
                "to shoot");
            break;
        case Phase::INTRO_3:
            session_.renderTutorialMessage(
                fdn,
                "Go ahead,",
                "give it a go!",
                "");
            break;
        case Phase::RULE_1:
            session_.renderTutorialMessage(
                fdn,
                "Very good!",
                "Now for an",
                "important rule");
            break;
        case Phase::RULE_2:
            session_.renderTutorialMessage(
                fdn,
                "I'm going to show",
                "a countdown:",
                "3... 2... 1... NOW");
            break;
        case Phase::RULE_3:
            session_.renderTutorialMessage(
                fdn,
                "Be careful not to",
                "shoot before I tell",
                "you to draw!");
            break;
        case Phase::READY:
            session_.renderReady(fdn, false);
            break;
        case Phase::TRY_AGAIN:
            session_.renderTutorialMessage(
                fdn,
                "I know you can",
                "do better!",
                "Try again...");
            break;
        case Phase::WIN_OUTRO_1:
            session_.renderTutorialMessage(
                fdn,
                "Nice work!",
                "You can quickdraw",
                "like a real alleycat!");
            break;
        case Phase::WIN_OUTRO_2:
            session_.renderTutorialMessage(
                fdn,
                "Now get yerself",
                "out there and start",
                "quickdrawin'!");
            break;
        case Phase::COUNTDOWN:
            break;
    }
}

void TutorialState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }

    controllerWirelessManager_->setMacPeer(command.wifiMacAddr);

    if (command.buttonId != ButtonIdentifier::SECONDARY_BUTTON ||
        command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    pendingBottomButton_ = true;
}

void TutorialState::handleBottomButton(FDN* fdn) {
    if (phase_ == Phase::COUNTDOWN || showingOutcome_) {
        if (session_.phase() == QuickdrawPracticePhase::DRAW ||
            session_.phase() == QuickdrawPracticePhase::COUNTDOWN) {
            session_.onPlayerShot(session_.phase() == QuickdrawPracticePhase::DRAW);
        }
        return;
    }

    if (phase_ == Phase::INTRO_3) {
        session_.pulsePlayerAck();
        advanceMessagePhase(fdn);
        return;
    }

    if (isMessagePhase(phase_)) {
        advanceMessagePhase(fdn);
    }
}

void TutorialState::beginReadyCountdown(FDN* fdn) {
    phase_ = Phase::READY;
    showingOutcome_ = false;
    messageTimer_.invalidate();
    session_.resetMatch();
    session_.setOpponentEnabled(false);
    session_.beginReady();
}

void TutorialState::handleMatchOutcome(FDN* fdn) {
    showingOutcome_ = true;

    if (session_.outcome() == QuickdrawPracticeOutcome::EARLY_SHOT ||
        session_.outcome() == QuickdrawPracticeOutcome::TIMEOUT) {
        session_.showLoseAnimation(fdn);
        return;
    }

    if (session_.outcome() == QuickdrawPracticeOutcome::WIN) {
        session_.showWinAnimation(fdn);
    }
}
