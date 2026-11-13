#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutProposal::ShootoutProposal(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_PROPOSAL)
    , shootoutManager(ctx.shootoutManager) {}

void ShootoutProposal::onStateMounted(PDN* pdn) {
    shootoutManager->startProposal();
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("SHOOTOUT", 15);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("press to", 40);
    d->drawCenteredText("confirm", 55);
    d->render();

    parameterizedCallbackFunction confirmCb = [](void* ctx) {
        static_cast<ShootoutProposal*>(ctx)->shootoutManager->confirmLocal();
    };
    pdn->getPrimaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);

    phaseSubscription.open(shootoutManager, [this](ShootoutManager::Phase phase) {
        transitionToBracketRevealState = phase == ShootoutManager::Phase::BRACKET_REVEAL;
    });
}

void ShootoutProposal::onStateDismounted(PDN* pdn) {
    phaseSubscription.close();
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    transitionToBracketRevealState = false;
}

bool ShootoutProposal::transitionToBracketReveal() const { return transitionToBracketRevealState; }
