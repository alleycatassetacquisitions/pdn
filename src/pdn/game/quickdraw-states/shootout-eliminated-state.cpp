#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutEliminated::ShootoutEliminated(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_ELIMINATED)
    , shootoutManager(ctx.shootoutManager) {}

void ShootoutEliminated::onStateMounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getLightManager()->stopAnimation();
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("OUT", 20);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("spectating", 50);
    d->render();

    phaseSubscription.open(shootoutManager, [this](ShootoutManager::Phase phase) {
        transitionToFinalStandingsState = phase == ShootoutManager::Phase::ENDED;
    });
}

void ShootoutEliminated::onStateDismounted(PDN* pdn) {
    phaseSubscription.close();
    transitionToFinalStandingsState = false;
}

bool ShootoutEliminated::transitionToFinalStandings() const {
    return transitionToFinalStandingsState;
}
