#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

class ProgressManager;

/*
 * KmgButtonAwarded — Reward state after beating an FDN on easy mode.
 *
 * Flow:
 * 1. Read pendingReward from Player (GameType of completed game)
 * 2. Unlock the corresponding Konami button
 * 3. Save progress via ProgressManager
 * 4. Display celebration message with button symbol
 * 5. After timeout, transition to GameOverReturn (returns to Quickdraw Idle)
 *
 * Note: Renamed from KonamiButtonAwarded to avoid ODR conflict with
 * quickdraw-states/konami-button-awarded.hpp
 */
class KmgButtonAwarded : public State {
public:
    KmgButtonAwarded(Player* player, ProgressManager* progressManager);
    ~KmgButtonAwarded() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToGameOver();

private:
    Player* player;
    ProgressManager* progressManager;
    SimpleTimer celebrationTimer;
    static constexpr int CELEBRATION_DURATION_MS = 4000;
    bool transitionToGameOverState = false;
    GameType unlockedGameType;
};

/*
 * KonamiBoonAwarded — Reward state after beating an FDN on hard mode.
 *
 * Flow:
 * 1. Read pendingReward from Player (GameType of completed game)
 * 2. Unlock the color profile for that game
 * 3. Save progress via ProgressManager
 * 4. Display celebration message with palette preview
 * 5. After timeout, transition to GameOverReturn (returns to Quickdraw Idle)
 */
class KonamiBoonAwarded : public State {
public:
    KonamiBoonAwarded(Player* player, ProgressManager* progressManager);
    ~KonamiBoonAwarded() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToGameOver();

private:
    Player* player;
    ProgressManager* progressManager;
    SimpleTimer celebrationTimer;
    static constexpr int CELEBRATION_DURATION_MS = 4000;
    bool transitionToGameOverState = false;
    GameType unlockedGameType;
};

/*
 * KonamiGameOverReturn — Final state in KonamiMetaGame flow.
 *
 * Displays a brief message and returns control to Quickdraw app (Idle state).
 * Used after:
 * - Easy mode loss
 * - Hard mode loss
 * - Replay completions (no rewards)
 * - Button/boon award celebrations
 */
class KonamiGameOverReturn : public State {
public:
    explicit KonamiGameOverReturn(Player* player);
    ~KonamiGameOverReturn() override;

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
