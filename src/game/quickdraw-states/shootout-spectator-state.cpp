#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include <cstdio>
#include <cstring>

ShootoutSpectator::ShootoutSpectator(ShootoutManager* shootout)
    : State(SHOOTOUT_SPECTATOR), shootout_(shootout) {}

static void drawSpectatorScreen(Device* PDN, ShootoutManager* shootout,
                                const std::pair<std::array<uint8_t,6>, std::array<uint8_t,6>>& pair) {
    std::string nameA = shootout->getNameForMac(pair.first.data());
    std::string nameB = shootout->getNameForMac(pair.second.data());
    auto* d = PDN->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("WATCHING", 5);
    d->drawCenteredText(nameA.c_str(), 25);
    d->drawCenteredText("vs", 40);
    d->drawCenteredText(nameB.c_str(), 55);
    d->render();
}

void ShootoutSpectator::onStateMounted(Device *PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    auto pair = shootout_->getCurrentMatchPair();
    lastDisplayedA_ = pair.first;
    lastDisplayedB_ = pair.second;
    drawSpectatorScreen(PDN, shootout_, pair);
}

void ShootoutSpectator::onStateLoop(Device *PDN) {
    shootout_->sync();
    auto p = shootout_->getPhase();
    if (p == ShootoutManager::Phase::MATCH_IN_PROGRESS && shootout_->isLocalDuelist()) {
        shouldGoToDuelCountdown_ = true;
    }
    if (p == ShootoutManager::Phase::ENDED) shouldGoToFinalStandings_ = true;
    if (p == ShootoutManager::Phase::ABORTED) shouldGoToAborted_ = true;

    auto pair = shootout_->getCurrentMatchPair();
    if (memcmp(pair.first.data(), lastDisplayedA_.data(), 6) != 0 ||
        memcmp(pair.second.data(), lastDisplayedB_.data(), 6) != 0) {
        lastDisplayedA_ = pair.first;
        lastDisplayedB_ = pair.second;
        drawSpectatorScreen(PDN, shootout_, pair);
    }
}

void ShootoutSpectator::onStateDismounted(Device *PDN) {
    shouldGoToDuelCountdown_ = false;
    shouldGoToFinalStandings_ = false;
    shouldGoToAborted_ = false;
}

bool ShootoutSpectator::transitionToDuelCountdown() { return shouldGoToDuelCountdown_; }
bool ShootoutSpectator::transitionToFinalStandings() { return shouldGoToFinalStandings_; }
bool ShootoutSpectator::transitionToAborted() { return shouldGoToAborted_; }
