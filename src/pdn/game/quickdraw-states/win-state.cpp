#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/bounty-win-animation.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"
#include <cstdio>

Win::Win(Player *player, ChainDuelManager* chainDuelManager, MatchManager* matchManager) : TypedState<PDN>(WIN) {
    this->player = player;
    this->chainDuelManager = chainDuelManager;
    this->matchManager = matchManager;
}

Win::~Win() {
    player = nullptr;
    matchManager = nullptr;
}

void Win::onStateMounted(PDN* pdn) {
    // Propagate the outcome to the supporter chain (no-op for non-champion).
    chainDuelManager->sendGameEventToSupporters(ChainGameEventType::WIN);

    pdn->getHaptics()->setIntensity(VIBRATION_OFF);

    pdn->getDisplay()->invalidateScreen()
        ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::WIN));

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
                unsigned long boosts = d.boostMs / ChainDuelManager::BOOST_PER_SUPPORTER_MS;
                snprintf(boostLine, sizeof(boostLine), "Boost: %lu", boosts);
                pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                    ->drawText(boostLine, 2, 28);
            }
            pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                ->drawText(oppLine, 2, 44);
        }
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
}

bool Win::resetGame() {
    return reset;
}

bool Win::isTerminalState() {
    return true;
}
