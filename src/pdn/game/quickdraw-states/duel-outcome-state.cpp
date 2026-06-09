#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/bounty-win-animation.hpp"
#include "device/animation/lose-animation.hpp"
#include "game/chain-manager.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"
#include <cstdio>

DuelOutcome::DuelOutcome(Player *player, ChainManager* chainManager, MatchManager* matchManager, bool won)
    : TypedState<PDN>(won ? WIN : LOSE), player(player), chainManager(chainManager),
      matchManager(matchManager), won_(won) {}

DuelOutcome::~DuelOutcome() {
    player = nullptr;
    matchManager = nullptr;
}

void DuelOutcome::onStateMounted(PDN* pdn) {
    // Propagate the outcome to the supporter chain (no-op for non-champion).
    chainManager->sendGameEventToSupporters(won_ ? ChainGameEventType::WIN : ChainGameEventType::LOSS);

    pdn->getHaptics()->setIntensity(VIBRATION_OFF);

    pdn->getDisplay()->invalidateScreen()
        ->drawImage(getImageForAllegiance(player->getAllegiance(),
                                          won_ ? ImageType::WIN : ImageType::LOSE));

    if (matchManager != nullptr) {
        const LastMatchDisplay& d = matchManager->getLastMatchDisplay();
        if (d.hasData) {
            pdn->getDisplay()->drawImage(Image(image_stats_card, 60, 64, 0, 0));
            char myLine[16];
            char boostLine[16];
            char oppLine[16];
            snprintf(myLine, sizeof(myLine), "You:%4lu", d.myTimeMs);
            snprintf(oppLine, sizeof(oppLine), "Opp:%4lu", d.opponentTimeMs);
            pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                ->drawText(myLine, 2, 12);
            if (d.boostMs > 0) {
                unsigned long boosts = d.boostMs / ChainManager::BOOST_PER_SUPPORTER_MS;
                snprintf(boostLine, sizeof(boostLine), "Boost: %lu", boosts);
                pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                    ->drawText(boostLine, 2, 28);
            }
            pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                ->drawText(oppLine, 2, 44);
        }
    }

    pdn->getDisplay()->render();

    outcomeTimer.setTimer(8000);

    AnimationConfig config;
    AnimationBase* animation = won_
        ? (player->isHunter() ? static_cast<AnimationBase*>(new HunterWinAnimation())
                              : static_cast<AnimationBase*>(new BountyWinAnimation()))
        : static_cast<AnimationBase*>(new LoseAnimation());
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;

    pdn->getLightManager()->startAnimation(animation, config);
}

void DuelOutcome::onStateLoop(PDN* pdn) {
    outcomeTimer.updateTime();
}

void DuelOutcome::onStateDismounted(PDN* pdn) {
    outcomeTimer.invalidate();
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
}

bool DuelOutcome::resetGame() {
    return outcomeTimer.expired();
}

bool DuelOutcome::isTerminalState() {
    return true;
}
