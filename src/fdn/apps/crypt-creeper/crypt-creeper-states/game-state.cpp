#include "apps/crypt-creeper/crypt-creeper-states.hpp"
#include "apps/crypt-creeper/maze-pages.hpp"
#include "apps/crypt-creeper/maze-page-bitmaps.hpp"
#include "device/animation/fdn-bounty-win-animation.hpp"
#include "device/drivers/button.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/crypt-creeper-glyphs.hpp"
#include "game/peripheral-glyphs.hpp"
#include "image.hpp"

namespace {
static const char* TAG = "CryptCreeperGameState";

using namespace CryptCreeperMaze;

int directionRowDelta(CreepDirection dir) {
    switch (dir) {
        case CreepDirection::UP:    return -1;
        case CreepDirection::DOWN:  return 1;
        default:                    return 0;
    }
}

int directionColDelta(CreepDirection dir) {
    switch (dir) {
        case CreepDirection::LEFT:  return -1;
        case CreepDirection::RIGHT: return 1;
        default:                    return 0;
    }
}

uint8_t directionGlyphId(CreepDirection dir) {
    switch (dir) {
        case CreepDirection::UP:    return static_cast<uint8_t>(PeripheralGlyphId::ARROW_UP);
        case CreepDirection::LEFT:  return static_cast<uint8_t>(PeripheralGlyphId::ARROW_LEFT);
        case CreepDirection::DOWN:  return static_cast<uint8_t>(PeripheralGlyphId::ARROW_DOWN);
        case CreepDirection::RIGHT: return static_cast<uint8_t>(PeripheralGlyphId::ARROW_RIGHT);
    }
    return static_cast<uint8_t>(PeripheralGlyphId::ARROW_RIGHT);
}

}  // namespace

CryptCreeperGameState::CryptCreeperGameState(ControllerWirelessManager* controllerWirelessManager,
                     unsigned long* elapsedMs)
    : TypedState<FDN>(CryptCreeperStateId::GAME)
    , controllerWirelessManager_(controllerWirelessManager)
    , elapsedMs_(elapsedMs) {}

CryptCreeperGameState::~CryptCreeperGameState() {}

void CryptCreeperGameState::resetGame() {
    phase_        = Phase::READY;
    currentPage_  = 0;
    direction_    = CreepDirection::RIGHT;
    hasReturnedFromPage4_ = false;
    transitionToScoringState_ = false;

    const MazePage& page = kPages[0];
    playerRow_ = page.startRow;
    playerCol_ = page.startCol;

    gameTimer_.invalidate();
    readyTimer_.invalidate();
    winTimer_.invalidate();
}

void CryptCreeperGameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    fdn_ = fdn;
    resetGame();

    RemoteDeviceCoordinator* remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr) {
            controllerWirelessManager_->setMacPeer(peerMac);
            controllerWirelessManager_->sendGameSelectPacket(GameSelectId::CONTROLLER_1);
        }
    }

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperGameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperGameState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);

    sendDirectionGlyph();
    readyTimer_.setTimer(kReadyDurationMs);
}

void CryptCreeperGameState::onStateLoop(FDN* fdn) {
    if (phase_ == Phase::READY) {
        renderReadyScreen(fdn);
        if (readyTimer_.expired()) {
            phase_ = Phase::PLAYING;
            gameTimer_.setTimer(0xFFFFFFFFUL);
            readyTimer_.invalidate();
        }
        return;
    }

    if (phase_ == Phase::WIN) {
        renderMaze(fdn);
        if (winTimer_.expired()) {
            clearWinAnimation(fdn);
            transitionToScoringState_ = true;
        }
        return;
    }

    renderMaze(fdn);
}

void CryptCreeperGameState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    clearWinAnimation(fdn);

    if (elapsedMs_ != nullptr && phase_ == Phase::PLAYING) {
        *elapsedMs_ = gameTimer_.getElapsedTime();
    }

    gameTimer_.invalidate();
    readyTimer_.invalidate();
    winTimer_.invalidate();
    transitionToScoringState_ = false;
    fdn_ = nullptr;
    controllerWirelessManager_->clearCallback();
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_DISPLAY, 0, 0);
}

bool CryptCreeperGameState::transitionToScoring() {
    return transitionToScoringState_;
}

void CryptCreeperGameState::onControllerCommandReceived(ControllerCommand command) {
    if (phase_ != Phase::PLAYING || fdn_ == nullptr) {
        return;
    }
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }
    if (command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    controllerWirelessManager_->setMacPeer(command.wifiMacAddr);

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        rotateDirectionCounterClockwise();
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        attemptMove(fdn_);
    }
}

void CryptCreeperGameState::rotateDirectionCounterClockwise() {
    direction_ = static_cast<CreepDirection>((static_cast<uint8_t>(direction_) + 1U) % 4U);
    sendDirectionGlyph();
}

void CryptCreeperGameState::sendDirectionGlyph() {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::DISPLAY_GLYPH,
        directionGlyphId(direction_),
        0);
}

void CryptCreeperGameState::pulseHaptic() {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_HAPTIC, 0, kWallHapticDurationTenths);
}

void CryptCreeperGameState::beginWinAnimation(FDN* fdn) {
    phase_ = Phase::WIN;

    if (elapsedMs_ != nullptr) {
        *elapsedMs_ = gameTimer_.getElapsedTime();
    }

    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::DISPLAY_ANIMATION, kWinAnimationId, 0);

    AnimationConfig config{};
    config.loop         = true;
    config.speed        = 16;
    config.loopDelayMs  = 0;
    config.initialState = LEDState();
    fdn->getLightManager()->startAnimation(new FDNBountyWinAnimation(), config);

    winTimer_.setTimer(kWinDurationMs);
}

void CryptCreeperGameState::clearWinAnimation(FDN* fdn) {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_LEDS, 0, 0);
    fdn->getLightManager()->stopAnimation();
    fdn->getLightManager()->clear();
}

PageLink CryptCreeperGameState::resolvePageLink(int newRow, int newCol) const {
    const MazePage& page = kPages[currentPage_];
    PageLink blocked{-1, 0, 0, 0, 0};

    if (newCol >= kGridCols) {
        return page.linkRight;
    }
    if (newCol < 0) {
        if (currentPage_ == kPage3 && !hasReturnedFromPage4_) {
            return blocked;
        }
        return page.linkLeft;
    }
    if (newRow < 0) {
        if (currentPage_ == kPage3 && hasReturnedFromPage4_) {
            return blocked;
        }
        return page.linkUp;
    }
    if (newRow >= kGridRows) {
        return page.linkDown;
    }
    return blocked;
}

bool CryptCreeperGameState::tryTransitionOffScreen(int newRow, int newCol) {
    if (newRow >= 0 && newRow < kGridRows && newCol >= 0 && newCol < kGridCols) {
        return false;
    }

    const PageLink link = resolvePageLink(newRow, newCol);
    if (!portalMatches(link, playerRow_, playerCol_)) {
        return false;
    }

    int entryRow = playerRow_;
    int entryCol = playerCol_;
    if (link.entryRow != kKeepPos) {
        entryRow = link.entryRow;
    }
    if (link.entryCol != kKeepPos) {
        entryCol = link.entryCol;
    }

    const MazePage& targetPage = kPages[link.targetPage];
    if (!isWalkable(targetPage, entryRow, entryCol)) {
        return false;
    }

    const int fromPage = currentPage_;
    currentPage_ = link.targetPage;
    playerRow_   = entryRow;
    playerCol_   = entryCol;

    if (fromPage == kPage4 && currentPage_ == kPage3) {
        hasReturnedFromPage4_ = true;
    }

    return true;
}

void CryptCreeperGameState::checkGoalReached(FDN* fdn) {
    const MazePage& page = kPages[currentPage_];
    if (page.goalRow >= 0 && page.goalCol >= 0 &&
        playerRow_ == page.goalRow && playerCol_ == page.goalCol) {
        beginWinAnimation(fdn);
    }
}

void CryptCreeperGameState::attemptMove(FDN* fdn) {
    const int newRow = playerRow_ + directionRowDelta(direction_);
    const int newCol = playerCol_ + directionColDelta(direction_);

    if (newRow >= 0 && newRow < kGridRows && newCol >= 0 && newCol < kGridCols) {
        if (isWall(kPages[currentPage_], newRow, newCol)) {
            pulseHaptic();
            return;
        }
        playerRow_ = newRow;
        playerCol_ = newCol;
        checkGoalReached(fdn);
        return;
    }

    if (tryTransitionOffScreen(newRow, newCol)) {
        checkGoalReached(fdn);
        return;
    }

    pulseHaptic();
}

void CryptCreeperGameState::renderReadyScreen(FDN* fdn) {
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText("READY?", 32)
        ->render();
}

void CryptCreeperGameState::renderMaze(FDN* fdn) {
    const MazePage& page = kPages[currentPage_];
    Display* d = fdn->getDisplay();

    Image pageImage(kPageBitmaps[currentPage_], kBitmapWidth, kBitmapHeight, 0, 0);
    d->invalidateScreen()
        ->drawImage(pageImage, 0, 0)
        ->setGlyphMode(FontMode::GRID_SYMBOL_GLYPH);

    if (page.goalRow >= 0 && page.goalCol >= 0) {
        d->renderGlyph(CryptCreeperGlyphs::kGoal,
                       page.goalCol * kCellSize,
                       page.goalRow * kCellSize + kGlyphBaseline);
    }

    d->renderGlyph(CryptCreeperGlyphs::kPlayer,
                   playerCol_ * kCellSize,
                   playerRow_ * kCellSize + kGlyphBaseline);
    d->render();
}
