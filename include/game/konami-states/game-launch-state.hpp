#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "game/fdn-game-type.hpp"
#include "utils/simple-timer.hpp"

/*
 * GameLaunchState — Base class for all game launch states in KonamiMetaGame.
 *
 * This state handles launching minigames with specific difficulty settings
 * and transitioning to appropriate reward states based on the outcome.
 *
 * State Layout:
 * - EasyLaunch (indices 1-7): First encounter, awards button on win
 * - ReplayEasy (indices 8-14): Button already earned, no rewards
 * - HardLaunch (indices 15-21): Hard mode unlocked, awards boon on win
 * - MasteryReplay (indices 22-28): Already mastered, launched from MasteryReplay menu
 *
 * Lifecycle:
 * 1. onStateMounted: Display game name, launch app via setActiveApp()
 * 2. onStatePaused: State machine pauses while minigame runs
 * 3. onStateResumed: Minigame completed, read outcome via getApp()
 * 4. onStateLoop: Transition based on outcome:
 *    - EasyLaunch + win → ButtonAwarded (index 29)
 *    - EasyLaunch + loss → GameOverReturn (index 31)
 *    - ReplayEasy → GameOverReturn (index 31)
 *    - HardLaunch + win → BoonAwarded (index 30)
 *    - HardLaunch + loss → GameOverReturn (index 31)
 *    - MasteryReplay → GameOverReturn (index 31)
 */

enum class LaunchMode {
    EASY_FIRST_ENCOUNTER,  // Awards button on win
    EASY_REPLAY,           // No reward
    HARD_FIRST_ENCOUNTER,  // Awards boon on win
    MASTERY_REPLAY         // No reward (launched from menu)
};

class GameLaunchState : public State {
public:
    GameLaunchState(int stateId, GameType gameType, LaunchMode mode);
    ~GameLaunchState() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    std::unique_ptr<Snapshot> onStatePaused(Device* PDN) override;
    void onStateResumed(Device* PDN, Snapshot* snapshot) override;

    bool transitionToButtonAwarded();
    bool transitionToBoonAwarded();
    bool transitionToGameOver();

private:
    GameType gameType;
    LaunchMode launchMode;
    bool gameLaunched;
    bool gameResumed;
    bool transitionToButtonAwardedState;
    bool transitionToBoonAwardedState;
    bool transitionToGameOverState;
    bool playerWon;
    SimpleTimer displayTimer;
    static constexpr int DISPLAY_DURATION_MS = 2000;

    void launchGame(Device* PDN);
    void readOutcome(Device* PDN);
    const char* getModeDisplayText();
};

struct GameLaunchSnapshot : public Snapshot {
    bool gameLaunched = false;
    bool gameResumed = false;
};

/*
 * Helper function to convert FdnGameType to GameType
 * FdnGameType ordering: SIGNAL_ECHO=0, GHOST_RUNNER=1, ..., KONAMI_CODE=7
 * GameType ordering: QUICKDRAW=0, GHOST_RUNNER=1, ..., SIGNAL_ECHO=7
 */
inline GameType fdnGameTypeToGameType(FdnGameType fdnType) {
    switch (fdnType) {
        case FdnGameType::SIGNAL_ECHO:       return GameType::SIGNAL_ECHO;
        case FdnGameType::GHOST_RUNNER:      return GameType::GHOST_RUNNER;
        case FdnGameType::SPIKE_VECTOR:      return GameType::SPIKE_VECTOR;
        case FdnGameType::FIREWALL_DECRYPT:  return GameType::FIREWALL_DECRYPT;
        case FdnGameType::CIPHER_PATH:       return GameType::CIPHER_PATH;
        case FdnGameType::EXPLOIT_SEQUENCER: return GameType::EXPLOIT_SEQUENCER;
        case FdnGameType::BREACH_DEFENSE:    return GameType::BREACH_DEFENSE;
        default:                             return GameType::SIGNAL_ECHO;
    }
}
