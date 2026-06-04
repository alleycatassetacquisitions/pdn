#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/lose-animation.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"
#include <cstdio>

Lose::Lose(Player *player, ChainDuelManager* chainDuelManager, MatchManager* matchManager) : TypedState<PDN>(LOSE) {
    this->player = player;
    this->chainDuelManager = chainDuelManager;
    this->matchManager = matchManager;
}

Lose::~Lose() {
    player = nullptr;
    matchManager = nullptr;
}

void Lose::onStateMounted(PDN* pdn) {
    chainDuelManager->sendGameEventToSupporters(ChainGameEventType::LOSS);

    pdn->getDisplay()->invalidateScreen()
        ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::LOSE));

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

    loseTimer.setTimer(8000);

    AnimationConfig config;
    config.type = AnimationType::LOSE;
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
}

bool Lose::resetGame() {
    return reset;
}

bool Lose::isTerminalState() {
    return true;
}
