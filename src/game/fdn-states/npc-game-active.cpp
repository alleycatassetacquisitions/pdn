#include "game/fdn-states.hpp"
#include "game/fdn-game.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NpcGameActive";

NpcGameActive::NpcGameActive(FdnGame* game) :
    State(FdnStateId::NPC_GAME_ACTIVE),
    game(game)
{
}

NpcGameActive::~NpcGameActive() {
    game = nullptr;
}

void NpcGameActive::onStateMounted(Device* PDN) {
    LOG_I(TAG, "NPC Game Active mounted");

    transitionToReceiveResultState = false;
    transitionToIdleState = false;

    inactivityTimer.setTimer(INACTIVITY_TIMEOUT_MS);
}

void NpcGameActive::onStateLoop(Device* PDN) {
    inactivityTimer.updateTime();
    if (inactivityTimer.expired()) {
        LOG_W(TAG, "Inactivity timeout — returning to idle");
        transitionToIdleState = true;
    }
}

void NpcGameActive::onStateDismounted(Device* PDN) {
    inactivityTimer.invalidate();
}

bool NpcGameActive::transitionToReceiveResult() {
    return transitionToReceiveResultState;
}

bool NpcGameActive::transitionToIdle() {
    return transitionToIdleState;
}
