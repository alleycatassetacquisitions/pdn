#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include <cstdio>
#include <cstring>

ShootoutFinalStandings::ShootoutFinalStandings(ShootoutManager* shootout, ChainDuelManager* chainDuelManager)
    : TypedState<PDN>(SHOOTOUT_FINAL_STANDINGS), shootout_(shootout), chainDuelManager_(chainDuelManager) {}

void ShootoutFinalStandings::onStateMounted(PDN* pdn) {
    pdn->getLightManager()->stopAnimation();
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
    if (shootout_) shootout_->sync();
    if (chainDuelManager_ && !chainDuelManager_->isLoop()) {
        shouldGoToSleep_ = true;
    }
}

void ShootoutFinalStandings::onStateDismounted(PDN* pdn) {
    // Reset Shootout state so the next loop closure triggers a fresh
    // proposal. Without this, phase_ stays ENDED and shootoutManager->active()
    // returns true, blocking the Idle→ShootoutProposal transition.
    if (shootout_) shootout_->resetToIdle();
    shouldGoToSleep_ = false;
}

bool ShootoutFinalStandings::isTerminalState() { return false; }

bool ShootoutFinalStandings::transitionToSleep() { return shouldGoToSleep_; }
