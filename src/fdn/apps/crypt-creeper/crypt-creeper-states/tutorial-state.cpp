#include "apps/crypt-creeper/crypt-creeper-states.hpp"
#include "apps/crypt-creeper/tutorial-pages.hpp"
#include "apps/crypt-creeper/tutorial-page-bitmaps.hpp"
#include "device/drivers/button.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/crypt-creeper-glyphs.hpp"
#include "game/peripheral-glyphs.hpp"
#include "image.hpp"
#include <functional>

namespace {
static const char* TAG = "CryptCreeperTutorialState";

using namespace CryptCreeperTutorial;
using CryptCreeperMaze::PageLink;
using CryptCreeperMaze::portalMatches;
using CryptCreeperMaze::kKeepPos;

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

CryptCreeperTutorialState::CryptCreeperTutorialState(
    ControllerWirelessManager* controllerWirelessManager)
    : TypedState<FDN>(CryptCreeperStateId::TUTORIAL)
    , controllerWirelessManager_(controllerWirelessManager) {}

CryptCreeperTutorialState::~CryptCreeperTutorialState() {}

void CryptCreeperTutorialState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    fdn_ = fdn;
    phase_ = Phase::WELCOME;
    transitionToMainMenuState_ = false;
    allowTurn_ = false;
    pendingTurnMessage_ = false;
    messageTimer_.setTimer(kMessageDurationMs);

    RemoteDeviceCoordinator* remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr) {
            controllerWirelessManager_->setMacPeer(peerMac);
            controllerWirelessManager_->sendGameSelectPacket(GameSelectId::CONTROLLER_1);
        }
    }

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperTutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperTutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);

    parameterizedCallbackFunction onTertiary = [](void* ctx) {
        static_cast<CryptCreeperTutorialState*>(ctx)->transitionToMainMenuState_ = true;
    };
    fdn->getTertiaryButton()->setButtonPress(onTertiary, this, ButtonInteraction::PRESS);
}

void CryptCreeperTutorialState::onStateLoop(FDN* fdn) {
    switch (phase_) {
        case Phase::WELCOME:
            renderMessage(fdn, "Welcome to", "Crypt Creeper!");
            if (messageTimer_.expired()) {
                phase_ = Phase::NAVIGATE;
                messageTimer_.setTimer(kMessageDurationMs);
            }
            break;

        case Phase::NAVIGATE:
            renderMessage(fdn, "Navigate through the", "labyrinth to the goal");
            if (messageTimer_.expired()) {
                phase_ = Phase::MOVE;
                messageTimer_.setTimer(kMessageDurationMs);
            }
            break;

        case Phase::MOVE:
            renderMessage(fdn, "Press the bottom", "button to move");
            if (messageTimer_.expired()) {
                beginMaze(fdn);
            }
            break;

        case Phase::CORNER_PAUSE:
            renderMaze(fdn);
            if (messageTimer_.expired()) {
                phase_ = Phase::TURN_MESSAGE;
                messageTimer_.setTimer(kMessageDurationMs);
            }
            break;

        case Phase::TURN_MESSAGE:
            renderMessage(fdn, "To change direction,", "press the top button");
            if (messageTimer_.expired()) {
                phase_ = Phase::MAZE;
                allowTurn_ = true;
                sendDirectionGlyph();
            }
            break;

        case Phase::MAZE:
            renderMaze(fdn);
            break;

        case Phase::NICE_WORK:
            renderMessage(fdn, "Nice Work!");
            if (messageTimer_.expired()) {
                phase_ = Phase::READY_REAL;
                messageTimer_.setTimer(kMessageDurationMs);
            }
            break;

        case Phase::READY_REAL:
            renderMessage(fdn, "Ready for the", "real crypt?");
            if (messageTimer_.expired()) {
                transitionToMainMenuState_ = true;
            }
            break;
    }
}

void CryptCreeperTutorialState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    fdn_ = nullptr;
    transitionToMainMenuState_ = false;
    messageTimer_.invalidate();
    controllerWirelessManager_->clearCallback();
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_DISPLAY, 0, 0);
    fdn->getTertiaryButton()->removeButtonCallbacks();
}

bool CryptCreeperTutorialState::transitionToMainMenu() {
    return transitionToMainMenuState_;
}

void CryptCreeperTutorialState::advanceMessagePhase() {
    messageTimer_.invalidate();

    if (phase_ == Phase::WELCOME) {
        phase_ = Phase::NAVIGATE;
        messageTimer_.setTimer(kMessageDurationMs);
    } else if (phase_ == Phase::NAVIGATE) {
        phase_ = Phase::MOVE;
        messageTimer_.setTimer(kMessageDurationMs);
    } else if (phase_ == Phase::MOVE) {
        beginMaze(fdn_);
    } else if (phase_ == Phase::TURN_MESSAGE) {
        phase_ = Phase::MAZE;
        allowTurn_ = true;
        sendDirectionGlyph();
    } else if (phase_ == Phase::NICE_WORK) {
        phase_ = Phase::READY_REAL;
        messageTimer_.setTimer(kMessageDurationMs);
    } else if (phase_ == Phase::READY_REAL) {
        transitionToMainMenuState_ = true;
    }
}

void CryptCreeperTutorialState::beginMaze(FDN* fdn) {
    phase_ = Phase::MAZE;
    currentPage_ = kPage1;
    direction_ = CreepDirection::RIGHT;
    allowTurn_ = false;
    pendingTurnMessage_ = false;

    const TutorialPage& page = kPages[kPage1];
    playerRow_ = page.startRow;
    playerCol_ = page.startCol;

    sendDirectionGlyph();
    renderMaze(fdn);
}

void CryptCreeperTutorialState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }
    if (command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    controllerWirelessManager_->setMacPeer(command.wifiMacAddr);

    if (phase_ == Phase::WELCOME || phase_ == Phase::NAVIGATE || phase_ == Phase::MOVE ||
        phase_ == Phase::TURN_MESSAGE || phase_ == Phase::NICE_WORK || phase_ == Phase::READY_REAL) {
        if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
            advanceMessagePhase();
        }
        return;
    }

    if (phase_ != Phase::MAZE || fdn_ == nullptr) {
        return;
    }

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        if (allowTurn_) {
            rotateDirectionCounterClockwise();
        }
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        attemptMove(fdn_);
    }
}

void CryptCreeperTutorialState::renderMessage(FDN* fdn, const char* line1, const char* line2) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    if (line2 != nullptr) {
        d->drawCenteredText(line1, 24);
        d->drawCenteredText(line2, 44);
    } else {
        d->drawCenteredText(line1, 32);
    }
    d->render();
}

void CryptCreeperTutorialState::sendDirectionGlyph() {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::DISPLAY_GLYPH,
        directionGlyphId(direction_),
        0);
}

void CryptCreeperTutorialState::pulseHaptic() {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_HAPTIC, 0, kWallHapticDurationTenths);
}

void CryptCreeperTutorialState::rotateDirectionCounterClockwise() {
    direction_ = static_cast<CreepDirection>((static_cast<uint8_t>(direction_) + 1U) % 4U);
    sendDirectionGlyph();
}

PageLink CryptCreeperTutorialState::resolvePageLink(int newRow, int newCol) const {
    const TutorialPage& page = kPages[currentPage_];
    PageLink blocked{-1, 0, 0, 0, 0};

    if (newCol >= kGridCols) {
        return page.linkRight;
    }
    if (newCol < 0) {
        return page.linkLeft;
    }
    if (newRow < 0) {
        return page.linkUp;
    }
    if (newRow >= kGridRows) {
        return blocked;
    }
    return blocked;
}

bool CryptCreeperTutorialState::tryTransitionOffScreen(int newRow, int newCol) {
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

    const TutorialPage& targetPage = kPages[link.targetPage];
    if (!isWalkable(targetPage, entryRow, entryCol)) {
        return false;
    }

    currentPage_ = link.targetPage;
    playerRow_   = entryRow;
    playerCol_   = entryCol;
    return true;
}

void CryptCreeperTutorialState::checkCornerAndGoal(FDN* fdn) {
    if (currentPage_ == kPage2 && playerRow_ == 2 && playerCol_ == 5 && !pendingTurnMessage_) {
        pendingTurnMessage_ = true;
        phase_ = Phase::CORNER_PAUSE;
        messageTimer_.setTimer(kCornerPauseMs);
        return;
    }

    const TutorialPage& page = kPages[currentPage_];
    if (page.goalRow >= 0 && page.goalCol >= 0 &&
        playerRow_ == page.goalRow && playerCol_ == page.goalCol) {
        phase_ = Phase::NICE_WORK;
        messageTimer_.setTimer(kMessageDurationMs);
        controllerWirelessManager_->sendPeripheralCommandPacket(
            PeripheralCmd::CLEAR_DISPLAY, 0, 0);
    }
}

void CryptCreeperTutorialState::attemptMove(FDN* fdn) {
    const int newRow = playerRow_ + directionRowDelta(direction_);
    const int newCol = playerCol_ + directionColDelta(direction_);

    if (newRow >= 0 && newRow < kGridRows && newCol >= 0 && newCol < kGridCols) {
        if (isWall(kPages[currentPage_], newRow, newCol)) {
            pulseHaptic();
            return;
        }
        playerRow_ = newRow;
        playerCol_ = newCol;
        checkCornerAndGoal(fdn);
        return;
    }

    if (tryTransitionOffScreen(newRow, newCol)) {
        checkCornerAndGoal(fdn);
        return;
    }

    pulseHaptic();
}

void CryptCreeperTutorialState::renderMaze(FDN* fdn) {
    const TutorialPage& page = kPages[currentPage_];
    Display* d = fdn->getDisplay();

    Image pageImage(kTutorialPageBitmaps[currentPage_], kBitmapWidth, kBitmapHeight, 0, 0);
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
