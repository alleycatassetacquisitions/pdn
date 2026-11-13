#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include <array>
#include <string>
#include <utility>

ShootoutSpectator::ShootoutSpectator(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_SPECTATOR)
    , shootoutManager(ctx.shootoutManager) {}

void ShootoutSpectator::renderMatchPair(PDN* pdn) {
    const std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> pair =
    shootoutManager->getCurrentMatchPair();
    std::string nameA = shootoutManager->getNameForMac(pair.first.data());
    std::string nameB = shootoutManager->getNameForMac(pair.second.data());
    Display* d = pdn->getDisplay();
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
    phaseSubscription.open(shootoutManager,
                           [this](ShootoutManager::Phase phase) { onPhaseEntered(phase); });
    // Paints the pair of a match already running, which the open has just
    // asked for.
    renderMatchPair(pdn);
    displayIsDirty = false;
}

void ShootoutSpectator::onPhaseEntered(ShootoutManager::Phase phase) {
    const bool matchStarted = phase == ShootoutManager::Phase::MATCH_IN_PROGRESS;
    transitionToMatchCountdownState = matchStarted && shootoutManager->isInCurrentMatch();
    transitionToFinalStandingsState = phase == ShootoutManager::Phase::ENDED;

    // Every match start names a new pair.
    if (matchStarted) displayIsDirty = true;
    // An abort zeroes the pair. Painting it would put WATCHING over two blank
    // names for the tick before the abort edge lifts this state.
    if (phase == ShootoutManager::Phase::ABORTED) displayIsDirty = false;
}

void ShootoutSpectator::onStateLoop(PDN* pdn) {
    if (!displayIsDirty) return;
    displayIsDirty = false;
    renderMatchPair(pdn);
}

void ShootoutSpectator::onStateDismounted(PDN* pdn) {
    phaseSubscription.close();
    transitionToMatchCountdownState = false;
    transitionToFinalStandingsState = false;
    displayIsDirty = false;
}

bool ShootoutSpectator::transitionToMatchCountdown() const {
    return transitionToMatchCountdownState;
}

bool ShootoutSpectator::transitionToFinalStandings() const {
    return transitionToFinalStandingsState;
}
