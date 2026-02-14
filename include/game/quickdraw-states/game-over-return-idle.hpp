#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

/*
 * GameOverReturnIdle — Brief "GAME OVER" or "TRY AGAIN" message before returning to Quickdraw idle.
 *
 * Lifecycle:
 * - onStateMounted: Starts brief delay timer, displays summary message
 * - onStateLoop: After timeout, transitions back to Quickdraw idle app via PDN->setActiveApp()
 * - onStateDismounted: Cleans up any active game state
 *
 * This state is the final state after a minigame loss or after celebrating a victory.
 * It ensures clean transition back to the main Quickdraw idle state.
 */
class GameOverReturnIdle : public State {
public:
    GameOverReturnIdle();
    ~GameOverReturnIdle() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

private:
    SimpleTimer returnTimer;
    bool displayIsDirty = true;
    bool shouldReturnToIdle = false;

    static constexpr int RETURN_DELAY_MS = 2000;

    void renderGameOverScreen(Device* PDN);
};
