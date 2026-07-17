#include "apps/floaty-boat/floaty-boat-states.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "game/floaty-boat-glyphs.hpp"
#include <cstdlib>
#include <string>

namespace {
static const char* TAG = "GameState";
}

GameState::GameState(ControllerWirelessManager* controllerWirelessManager, int* primaryScore)
    : TypedState<FDN>(FloatyBoatStateId::GAME)
    , controllerWirelessManager_(controllerWirelessManager)
    , primaryScore_(primaryScore) {}

GameState::~GameState() {}

void GameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    phase_ = Phase::READY;
    currentLane_ = 1;
    liveScore_ = 0;
    transitionToScoringState_ = false;
    collisionTimer_.invalidate();

    if (primaryScore_) {
        *primaryScore_ = 0;
    }

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
        std::bind(&GameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&GameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);

    parameterizedCallbackFunction onPrimary = [](void* ctx) {
        static_cast<GameState*>(ctx)->moveLaneUp();
    };
    parameterizedCallbackFunction onSecondary = [](void* ctx) {
        static_cast<GameState*>(ctx)->moveLaneDown();
    };

    fdn->getPrimaryButton()->setButtonPress(onPrimary, this, ButtonInteraction::PRESS);
    fdn->getSecondaryButton()->setButtonPress(onSecondary, this, ButtonInteraction::PRESS);

    readyTimer_.setTimer(kReadyDurationMs);
    renderReadyScreen(fdn);
}

void GameState::onStateLoop(FDN* fdn) {
    switch (phase_) {
        case Phase::READY:
            renderReadyScreen(fdn);
            if (readyTimer_.expired()) {
                phase_ = Phase::PLAYING;
                readyTimer_.invalidate();
                barrierController_.start();
            }
            break;

        case Phase::PLAYING: {
            barrierController_.updateMovementAndSpawns();
            checkCollision();
            if (phase_ == Phase::PLAYING) {
                liveScore_ += barrierController_.removeClearedColumns();
                if (primaryScore_) {
                    *primaryScore_ = liveScore_;
                }
            }
            renderPlayScreen(fdn);
            break;
        }

        case Phase::COLLISION:
            renderPlayScreen(fdn);
            if (collisionTimer_.expired()) {
                if (primaryScore_) {
                    *primaryScore_ = liveScore_;
                }
                transitionToScoringState_ = true;
            }
            break;
    }
}

void GameState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted — score=%d", primaryScore_ ? *primaryScore_ : 0);
    readyTimer_.invalidate();
    collisionTimer_.invalidate();
    fdn->getPrimaryButton()->removeButtonCallbacks();
    fdn->getSecondaryButton()->removeButtonCallbacks();
    controllerWirelessManager_->clearCallback();
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_DISPLAY, 0, 0);
}

bool GameState::transitionToScoring() {
    return transitionToScoringState_;
}

void GameState::moveLaneUp() {
    if (phase_ != Phase::PLAYING) {
        return;
    }
    if (currentLane_ == 0) {
        return;
    }
    currentLane_--;
}

void GameState::moveLaneDown() {
    if (phase_ != Phase::PLAYING) {
        return;
    }
    if (currentLane_ == 2) {
        return;
    }
    currentLane_++;
}

void GameState::checkCollision() {
    for (const auto& barrier : barrierController_.getBarriers()) {
        if (barrier.lane == currentLane_ && barrier.xCoord < kCollisionXThreshold) {
            LOG_W(TAG, "Collision in lane %d at x=%d", currentLane_, barrier.xCoord);
            phase_ = Phase::COLLISION;
            barrierController_.freeze();
            collisionTimer_.setTimer(kCollisionDurationMs);
            return;
        }
    }
}

void GameState::renderReadyScreen(FDN* fdn) {
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawCenteredText("READY?", 32)
        ->render();
}

void GameState::renderPlayScreen(FDN* fdn) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen();

    renderPlayer(display);
    renderBarriers(display);
    renderScore(display);
    renderExplosion(display);

    display->render();
}

void GameState::renderPlayer(Display* display) {
    int y = 42;
    switch (currentLane_) {
        case 0:
            y = 20;
            break;
        case 1:
            y = 42;
            break;
        case 2:
            y = 63;
            break;
    }

    display->setGlyphMode(FontMode::ARCADE_1)
        ->renderGlyph(FloatyBoatGlyphs::kBoat, kPlayerX, y);
}

void GameState::renderBarriers(Display* display) {
    display->setGlyphMode(FontMode::ARCADE_1);
    for (const auto& barrier : barrierController_.getBarriers()) {
        display->renderGlyph(barrier.glyph, barrier.xCoord, barrier.lane * 21 + 20);
    }
}

void GameState::renderScore(Display* display) {
    std::string scoreText = std::to_string(liveScore_);
    display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawText(scoreText.c_str(), 110, 10);
}

void GameState::renderExplosion(Display* display) {
    if (phase_ != Phase::COLLISION) {
        return;
    }

    float progress = static_cast<float>(collisionTimer_.getElapsedTime()) /
                     static_cast<float>(kCollisionDurationMs);
    int radius = static_cast<int>(progress * kMaxExplosionRadius);
    if (radius < 1) {
        return;
    }

    int centerX = 6;
    int centerY = currentLane_ * 21 + 14;
    display->drawFilledCircle(centerX, centerY, radius);
}

void GameState::onControllerCommandReceived(ControllerCommand command) {
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
