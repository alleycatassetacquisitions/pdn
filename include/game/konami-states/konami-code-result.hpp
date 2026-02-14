#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"

class ProgressManager;

/*
 * KonamiCodeAccepted - Success state after completing the Konami code
 *
 * - Unlocks hard mode (sets bit 7 of konamiProgress)
 * - Saves progress to NVS
 * - Displays: "You've unlocked Konami's Boon / Beat the FDNs again to steal their power"
 * - Provides celebration LED + haptic feedback
 * - After timeout, returns to Quickdraw app (setActiveApp)
 */
class KonamiCodeAccepted : public State {
public:
    KonamiCodeAccepted(Player* player, ProgressManager* progressManager);
    ~KonamiCodeAccepted() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToReturnQuickdraw();

private:
    Player* player;
    ProgressManager* progressManager;
    SimpleTimer displayTimer;
    static constexpr int DISPLAY_DURATION_MS = 5000;
    bool transitionToReturnQuickdrawState = false;
};

/*
 * KonamiCodeRejected - Rejection state when player hasn't collected all 7 buttons
 *
 * - Displays: "NOT READY / X of 7 buttons collected / Find more FDNs..."
 * - After timeout, returns to Quickdraw app (setActiveApp)
 */
class KonamiCodeRejected : public State {
public:
    explicit KonamiCodeRejected(Player* player);
    ~KonamiCodeRejected() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToReturnQuickdraw();

private:
    Player* player;
    SimpleTimer displayTimer;
    static constexpr int DISPLAY_DURATION_MS = 4000;
    bool transitionToReturnQuickdrawState = false;
};

/*
 * GameOverReturnIdle - Timeout state that returns to Idle
 *
 * - Brief display message
 * - Transitions back to Quickdraw Idle state
 */
class GameOverReturnIdle : public State {
public:
    explicit GameOverReturnIdle(Player* player);
    ~GameOverReturnIdle() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToReturnQuickdraw();

private:
    Player* player;
    SimpleTimer displayTimer;
    static constexpr int DISPLAY_DURATION_MS = 2000;
    bool transitionToReturnQuickdrawState = false;
};
