#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/bounty-win-animation.hpp"
#include "game/posse-manager.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"

Win::Win(const GameContext& ctx)
    : TypedState<PDN>(WIN)
    , player(ctx.player)
    , posseManager(ctx.posseManager)
    , matchManager(ctx.matchManager) {
}

void Win::onStateMounted(PDN* pdn) {
    // Propagate the outcome to the supporter chain (no-op for non-challenger).
    posseManager->sendGameEventToSupporters(ChainGameEventType::WIN);

    pdn->getHaptics()->setIntensity(VIBRATION_OFF);

    pdn->getDisplay()->invalidateScreen()
        ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::WIN));

    if (matchManager != nullptr) {
        renderLastMatchStats(pdn->getDisplay(), matchManager->getLastMatchDisplay());
    }

    pdn->getDisplay()->render();

    winTimer.setTimer(8000);

    AnimationBase* animation = player->isHunter()
        ? (AnimationBase*)new HunterWinAnimation()
        : (AnimationBase*)new BountyWinAnimation();
    AnimationConfig config;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;

    pdn->getLightManager()->startAnimation(animation, config);
}

void Win::onStateLoop(PDN* pdn) {
    winTimer.updateTime();
    if(winTimer.expired()) {
        reset = true;
    }
}

void Win::onStateDismounted(PDN* pdn) {
    winTimer.invalidate();
    reset = false;
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    // Sleep comes next and draws nothing on the LEDs; the looping win animation would run on.
    pdn->getLightManager()->stopAnimation();
}

bool Win::resetGame() {
    return reset;
}
