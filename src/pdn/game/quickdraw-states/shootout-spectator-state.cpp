#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include <array>
#include <cstdio>
#include <cstring>

ShootoutSpectator::ShootoutSpectator(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_SPECTATOR)
    , shootout(ctx.shootoutManager) {}

static void drawSpectatorScreen(PDN* pdn, ShootoutManager* shootout,
                                const std::pair<std::array<uint8_t,6>, std::array<uint8_t,6>>& pair) {
    std::string nameA = shootout->getNameForMac(pair.first.data());
    std::string nameB = shootout->getNameForMac(pair.second.data());
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("WATCHING", 5);
    d->drawCenteredText(nameA.c_str(), 25);
    d->drawCenteredText("vs", 40);
    d->drawCenteredText(nameB.c_str(), 55);
    d->render();
}

void ShootoutSpectator::onStateMounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getLightManager()->stopAnimation();
    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> pair =
    shootout->getCurrentMatchPair();
    lastDisplayedA_ = pair.first;
    lastDisplayedB_ = pair.second;
    drawSpectatorScreen(pdn, shootout, pair);
}

void ShootoutSpectator::onStateLoop(PDN* pdn) {
    ShootoutManager::Phase p = shootout->getPhase();
    if (p == ShootoutManager::Phase::MATCH_IN_PROGRESS && shootout->isInCurrentMatch()) {
        shouldGoToMatchCountdown = true;
    }
    if (p == ShootoutManager::Phase::ENDED) shouldGoToFinalStandings_ = true;

    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> pair =
    shootout->getCurrentMatchPair();
    // A teardown zeroes the pair, and an all-zero MAC names no player. Without
    // this the change test fires on the way out and repaints WATCHING over two
    // blank names, for the one tick before the abort edge lifts the state.
    const std::array<uint8_t, 6> noMatch{};
    if (memcmp(pair.first.data(), noMatch.data(), 6) == 0) return;
    if (memcmp(pair.first.data(), lastDisplayedA_.data(), 6) != 0 ||
        memcmp(pair.second.data(), lastDisplayedB_.data(), 6) != 0) {
        lastDisplayedA_ = pair.first;
        lastDisplayedB_ = pair.second;
        drawSpectatorScreen(pdn, shootout, pair);
    }
}

void ShootoutSpectator::onStateDismounted(PDN* pdn) {
    shouldGoToMatchCountdown = false;
    shouldGoToFinalStandings_ = false;
}

bool ShootoutSpectator::transitionToMatchCountdown() const { return shouldGoToMatchCountdown; }
bool ShootoutSpectator::transitionToFinalStandings() { return shouldGoToFinalStandings_; }
