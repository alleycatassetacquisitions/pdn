#include "apps/floaty-boat/floaty-boat-states.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "game/floaty-boat-glyphs.hpp"
#include <cstdlib>

namespace {
static const char* TAG = "TutorialState";
}

TutorialState::TutorialState(ControllerWirelessManager* controllerWirelessManager)
    : TypedState<FDN>(FloatyBoatStateId::TUTORIAL)
    , controllerWirelessManager_(controllerWirelessManager) {}

TutorialState::~TutorialState() {}

void TutorialState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    phase_ = Phase::WELCOME;
    currentLane_ = 1;
    dodgedCount_ = 0;
    transitionToMainMenuState_ = false;
    collisionTimer_.invalidate();
    messageTimer_.setTimer(kMessageDurationMs);
    showCurrentMessage(fdn);

    std::srand(static_cast<unsigned>(SimpleTimer::getPlatformClock()
                                         ? SimpleTimer::getPlatformClock()->milliseconds()
                                         : 1));

    RemoteDeviceCoordinator* remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr) {
            controllerWirelessManager_->setMacPeer(peerMac);
            controllerWirelessManager_->sendGameSelectPacket(GameSelectId::CONTROLLER_1);
        }
    }

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&TutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&TutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);

    parameterizedCallbackFunction onPrimary = [](void* ctx) {
        static_cast<TutorialState*>(ctx)->moveLaneUp();
    };
    parameterizedCallbackFunction onSecondary = [](void* ctx) {
        static_cast<TutorialState*>(ctx)->moveLaneDown();
    };
    parameterizedCallbackFunction onTertiary = [](void* ctx) {
        static_cast<TutorialState*>(ctx)->transitionToMainMenuState_ = true;
    };

    fdn->getPrimaryButton()->setButtonPress(onPrimary, this, ButtonInteraction::PRESS);
    fdn->getSecondaryButton()->setButtonPress(onSecondary, this, ButtonInteraction::PRESS);
    fdn->getTertiaryButton()->setButtonPress(onTertiary, this, ButtonInteraction::PRESS);
}

void TutorialState::onStateLoop(FDN* fdn) {
    switch (phase_) {
        case Phase::WELCOME:
        case Phase::CONTROLS_LANES:
        case Phase::CONTROLS_UP:
        case Phase::CONTROLS_DOWN:
        case Phase::DEMO_PROMPT:
            showCurrentMessage(fdn);
            if (messageTimer_.expired()) {
                advanceMessagePhase();
            }
            break;

        case Phase::DEMO:
            updateDemo(fdn);
            break;

        case Phase::COLLISION:
            renderDemoScreen(fdn);
            if (collisionTimer_.expired()) {
                phase_ = Phase::TRY_AGAIN;
                collisionTimer_.invalidate();
                messageTimer_.setTimer(kFeedbackDurationMs);
                showCurrentMessage(fdn);
            }
            break;

        case Phase::TRY_AGAIN:
            showCurrentMessage(fdn);
            if (messageTimer_.expired()) {
                beginDemo();
            }
            break;

        case Phase::NICE_WORK:
            showCurrentMessage(fdn);
            if (messageTimer_.expired()) {
                phase_ = Phase::READY_TO_PLAY;
                messageTimer_.setTimer(kFeedbackDurationMs);
                showCurrentMessage(fdn);
            }
            break;

        case Phase::READY_TO_PLAY:
            showCurrentMessage(fdn);
            if (messageTimer_.expired()) {
                transitionToMainMenuState_ = true;
            }
            break;
    }
}

void TutorialState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    messageTimer_.invalidate();
    collisionTimer_.invalidate();
    transitionToMainMenuState_ = false;
    fdn->getPrimaryButton()->removeButtonCallbacks();
    fdn->getSecondaryButton()->removeButtonCallbacks();
    fdn->getTertiaryButton()->removeButtonCallbacks();
    controllerWirelessManager_->clearCallback();
}

bool TutorialState::transitionToMainMenu() {
    return transitionToMainMenuState_;
}

void TutorialState::advanceMessagePhase() {
    messageTimer_.invalidate();

    switch (phase_) {
        case Phase::WELCOME:
            phase_ = Phase::CONTROLS_LANES;
            messageTimer_.setTimer(kMessageDurationMs);
            break;
        case Phase::CONTROLS_LANES:
            phase_ = Phase::CONTROLS_UP;
            messageTimer_.setTimer(kMessageDurationMs);
            break;
        case Phase::CONTROLS_UP:
            phase_ = Phase::CONTROLS_DOWN;
            messageTimer_.setTimer(kMessageDurationMs);
            break;
        case Phase::CONTROLS_DOWN:
            phase_ = Phase::DEMO_PROMPT;
            messageTimer_.setTimer(kMessageDurationMs);
            break;
        case Phase::DEMO_PROMPT:
            beginDemo();
            return;
        default:
            return;
    }
}

void TutorialState::beginDemo() {
    phase_ = Phase::DEMO;
    currentLane_ = 1;
    dodgedCount_ = 0;
    messageTimer_.invalidate();
    collisionTimer_.invalidate();
    barrierController_.start(BarrierController::Mode::EASY);
}

void TutorialState::updateDemo(FDN* fdn) {
    barrierController_.updateMovementAndSpawns();
    checkCollision();
    if (phase_ != Phase::DEMO) {
        renderDemoScreen(fdn);
        return;
    }

    dodgedCount_ += barrierController_.removeClearedColumns();
    if (dodgedCount_ >= kObstaclesToDodge) {
        barrierController_.freeze();
        phase_ = Phase::NICE_WORK;
        messageTimer_.setTimer(kFeedbackDurationMs);
        showCurrentMessage(fdn);
        return;
    }

    renderDemoScreen(fdn);
}

void TutorialState::moveLaneUp() {
    if (phase_ != Phase::DEMO) {
        return;
    }
    if (currentLane_ == 0) {
        return;
    }
    currentLane_--;
}

void TutorialState::moveLaneDown() {
    if (phase_ != Phase::DEMO) {
        return;
    }
    if (currentLane_ == 2) {
        return;
    }
    currentLane_++;
}

void TutorialState::checkCollision() {
    for (const auto& barrier : barrierController_.getBarriers()) {
        if (barrier.lane == currentLane_ && barrier.xCoord < kCollisionXThreshold) {
            phase_ = Phase::COLLISION;
            barrierController_.freeze();
            collisionTimer_.setTimer(kCollisionDurationMs);
            return;
        }
    }
}

void TutorialState::renderDemoScreen(FDN* fdn) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen();

    int playerY = 42;
    switch (currentLane_) {
        case 0:
            playerY = 20;
            break;
        case 1:
            playerY = 42;
            break;
        case 2:
            playerY = 63;
            break;
    }

    display->setGlyphMode(FontMode::ARCADE_1)
        ->renderGlyph(FloatyBoatGlyphs::kBoat, kPlayerX, playerY);

    for (const auto& barrier : barrierController_.getBarriers()) {
        display->renderGlyph(barrier.glyph, barrier.xCoord, barrier.lane * 21 + 20);
    }

    if (phase_ == Phase::COLLISION) {
        float progress = static_cast<float>(collisionTimer_.getElapsedTime()) /
                         static_cast<float>(kCollisionDurationMs);
        int radius = static_cast<int>(progress * kMaxExplosionRadius);
        if (radius >= 1) {
            display->drawFilledCircle(6, currentLane_ * 21 + 14, radius);
        }
    }

    display->render();
}

void TutorialState::showCurrentMessage(FDN* fdn) {
    switch (phase_) {
        case Phase::WELCOME:
            renderTutorialMessage(fdn, "Welcome to", "Floaty Boat!");
            break;
        case Phase::CONTROLS_LANES:
            renderTutorialMessage(fdn, "Steer your boat", "between three lanes");
            break;
        case Phase::CONTROLS_UP:
            renderTutorialMessage(fdn, "Top button", "moves UP");
            break;
        case Phase::CONTROLS_DOWN:
            renderTutorialMessage(fdn, "Bottom button", "moves DOWN");
            break;
        case Phase::DEMO_PROMPT:
            renderTutorialMessage(fdn, "Try dodging", "five obstacles!");
            break;
        case Phase::TRY_AGAIN:
            renderTutorialMessage(fdn, "Try again!");
            break;
        case Phase::NICE_WORK:
            renderTutorialMessage(fdn, "Nice work!");
            break;
        case Phase::READY_TO_PLAY:
            renderTutorialMessage(fdn, "Ready to play?");
            break;
        default:
            break;
    }
}

void TutorialState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }
    if (command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    controllerWirelessManager_->setMacPeer(command.wifiMacAddr);

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        moveLaneUp();
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        moveLaneDown();
    }
}
