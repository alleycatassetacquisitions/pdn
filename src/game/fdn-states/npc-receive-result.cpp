#include "game/fdn-states.hpp"
#include "game/fdn-game.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NpcReceiveResult";

NpcReceiveResult::NpcReceiveResult(FdnGame* game, FdnResultManager* resultManager) :
    State(FdnStateId::NPC_RECEIVE_RESULT),
    game(game),
    resultManager(resultManager)
{
}

NpcReceiveResult::~NpcReceiveResult() {
    game = nullptr;
    resultManager = nullptr;
}

void NpcReceiveResult::onStateMounted(Device* PDN) {
    LOG_I(TAG, "NPC Receive Result mounted — won: %d, score: %d",
          game->getLastResult(), game->getLastScore());

    transitionToIdleState = false;
    displayTimer.setTimer(DISPLAY_DURATION_MS);

    if (resultManager) {
        // Note: NPC doesn't receive difficulty info from player, so we default to false (easy)
        resultManager->cacheResult(game->getGameType(), game->getLastResult(), game->getLastScore(), false);
    }

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    if (game->getLastResult()) {
        PDN->getDisplay()->drawText("PLAYER WON", 15, 25);
    } else {
        PDN->getDisplay()->drawText("PLAYER LOST", 15, 25);
    }
    std::string scoreStr = "Score: " + std::to_string(game->getLastScore());
    PDN->getDisplay()->drawText(scoreStr.c_str(), 15, 45);
    PDN->getDisplay()->render();
}

void NpcReceiveResult::onStateLoop(Device* PDN) {
    displayTimer.updateTime();
    if (displayTimer.expired()) {
        transitionToIdleState = true;
    }
}

void NpcReceiveResult::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();
}

bool NpcReceiveResult::transitionToIdle() {
    return transitionToIdleState;
}
