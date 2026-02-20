#include "apps/speeder/speeder-app-states.hpp"
#include "device/device.hpp"
#include "apps/speeder/speeder-app-resources.hpp"
#include "device/drivers/logger.hpp"

GameOver::GameOver() : State(GAME_OVER) {}

void GameOver::onStateMounted(Device *PDN) {
    LOG_I("GameOver", "State mounted - initializing display and buttons");

    PDN->getDisplay()->invalidateScreen()->
    drawImage(loseImage)->
    render();

    gameOverTimer.setTimer(GAME_OVER_DURATION);
}

void GameOver::onStateLoop(Device *PDN) {
    if(gameOverTimer.expired()) {
        transitionToTryAgainState = true;
    }
}

bool GameOver::shouldTransitionToTryAgain() const {
    return transitionToTryAgainState;
}

void GameOver::onStateDismounted(Device *PDN) {
    gameOverTimer.invalidate();
    transitionToTryAgainState = false;
}