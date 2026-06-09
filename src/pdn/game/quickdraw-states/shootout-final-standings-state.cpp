#include "game/quickdraw-states.hpp"
#include <cstring>

ShootoutFinalStandings::ShootoutFinalStandings(ShootoutManager* shootout, ChainManager* chainManager)
    : TypedState<PDN>(SHOOTOUT_FINAL_STANDINGS), shootout_(shootout), chainManager_(chainManager) {}

void ShootoutFinalStandings::onStateMounted(PDN* pdn) {
    pdn->getLightManager()->stopAnimation();
    displayTimer_.setTimer(STANDINGS_DISPLAY_MS);
    auto winner = shootout_->getTournamentWinner();
    const uint8_t* selfMac = pdn->getWirelessManager()->getMacAddress();
    bool iWon = selfMac && memcmp(winner.data(), selfMac, 6) == 0;
    std::string winnerName = shootout_->getNameForMac(winner.data());
    const char* header = iWon ? "VICTORY" : "DEFEAT";
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText(header, 20);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("winner:", 40);
    d->drawCenteredText(winnerName.c_str(), 55);
    d->render();
}

void ShootoutFinalStandings::onStateLoop(PDN* pdn) {
    // Exit to Idle so the device stays playable without a power-cycle. An
    // intact ring at timeout re-fires Idle->ShootoutProposal: back-to-back
    // tournaments by design.
    auto* rdc = pdn ? pdn->getRemoteDeviceCoordinator() : nullptr;
    if (rdc == nullptr) return;
    bool ringStillClosed = (rdc->getPeerMac(SerialIdentifier::OUTPUT_JACK) != nullptr) &&
                           (rdc->getPeerMac(SerialIdentifier::INPUT_JACK) != nullptr);
    if (!ringStillClosed || displayTimer_.expired()) {
        shouldGoToIdle_ = true;
    }
}

void ShootoutFinalStandings::onStateDismounted(PDN* pdn) {
    // Reset Shootout state so the next loop closure triggers a fresh
    // proposal. Without this, phase_ stays ENDED and shootoutManager->active()
    // returns true, blocking the Idle→ShootoutProposal transition.
    if (shootout_) shootout_->resetToIdle();
    displayTimer_.invalidate();
    shouldGoToIdle_ = false;
}

bool ShootoutFinalStandings::isTerminalState() { return false; }

bool ShootoutFinalStandings::transitionToIdle() { return shouldGoToIdle_; }
