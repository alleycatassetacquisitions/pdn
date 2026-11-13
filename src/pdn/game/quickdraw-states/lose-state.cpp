#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/lose-animation.hpp"
#include "game/posse-manager.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"

Lose::Lose(const GameContext& ctx)
    : TypedState<PDN>(LOSE)
    , player(ctx.player)
    , posseManager(ctx.posseManager)
    , matchManager(ctx.matchManager) {
}

void Lose::onStateMounted(PDN* pdn) {
    posseManager->sendGameEventToSupporters(ChainGameEventType::LOSS);

    pdn->getDisplay()->invalidateScreen()
        ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::LOSE));

    if (matchManager != nullptr) {
        renderLastMatchStats(pdn->getDisplay(), matchManager->getLastMatchDisplay());
    }

    pdn->getDisplay()->render();

    loseTimer.setTimer(8000);

    AnimationConfig config;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;

    pdn->getLightManager()->startAnimation(new LoseAnimation(), config);
}

void Lose::onStateLoop(PDN* pdn) {
    loseTimer.updateTime();
    if(loseTimer.expired()) {
        reset = true;
    }
}

void Lose::onStateDismounted(PDN* pdn) {
    loseTimer.invalidate();
    reset = false;
    // Sleep comes next and draws nothing on the LEDs; the looping lose animation would run on.
    pdn->getLightManager()->stopAnimation();
}

bool Lose::resetGame() {
    return reset;
}
