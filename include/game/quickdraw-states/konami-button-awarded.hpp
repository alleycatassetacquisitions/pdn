#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

// Forward declaration
class ProgressManager;

/*
 * KonamiButtonAwarded — Shows victory screen after winning easy mode minigame.
 *
 * Lifecycle:
 * - onStateMounted: Unlocks the button for the game that was just won,
 *   persists progress to NVS, starts victory timer
 * - onStateLoop: Displays "BUTTON UNLOCKED!" message with game name and count,
 *   triggers victory LED pattern + haptic feedback
 * - Transitions to GameOverReturnIdle after 3-5 second timeout
 *
 * This state is responsible for:
 * - Calling player->unlockKonamiButton(buttonIndex) to set bit in konamiProgress
 * - Persisting progress via ProgressManager->saveProgress()
 * - Displaying celebration UI
 */
class KonamiButtonAwarded : public State {
public:
    KonamiButtonAwarded(GameType gameType, Player* player, ProgressManager* progressManager);
    ~KonamiButtonAwarded() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToGameOverReturnIdle();

private:
    GameType gameType;
    Player* player;
    ProgressManager* progressManager;
    SimpleTimer victoryTimer;
    bool transitionToGameOverReturnIdleState = false;
    bool displayIsDirty = true;

    static constexpr int VICTORY_DISPLAY_DURATION_MS = 4000;

    void renderVictoryScreen(Device* PDN);
    int countUnlockedButtons() const;
};
