#pragma once

#include "game/player.hpp"
#include "game/fdn-game-type.hpp"
#include "state/state-machine.hpp"
#include "state/state.hpp"

constexpr int KONAMI_METAGAME_APP_ID = 9;

/*
 * KonamiMetaGame state IDs within the KonamiMetaGame state machine.
 * Layout: 35 states total
 * [0] = Handshake
 * [1-7] = EasyLaunch (7 games)
 * [8-14] = ReplayEasy (7 games)
 * [15-21] = HardLaunch (7 games)
 * [22-28] = MasteryReplay (7 games)
 * [29] = ButtonAwarded
 * [30] = BoonAwarded
 * [31] = GameOverReturn
 * [32] = CodeEntry
 * [33] = CodeAccepted
 * [34] = CodeRejected
 */
enum KonamiMetaGameStateId {
    KONAMI_HANDSHAKE = 0,
    KONAMI_EASY_LAUNCH_START = 1,
    KONAMI_EASY_LAUNCH_END = 7,
    KONAMI_REPLAY_EASY_START = 8,
    KONAMI_REPLAY_EASY_END = 14,
    KONAMI_HARD_LAUNCH_START = 15,
    KONAMI_HARD_LAUNCH_END = 21,
    KONAMI_MASTERY_REPLAY_START = 22,
    KONAMI_MASTERY_REPLAY_END = 28,
    KONAMI_BUTTON_AWARDED = 29,
    KONAMI_BOON_AWARDED = 30,
    KONAMI_GAME_OVER_RETURN = 31,
    KONAMI_CODE_ENTRY = 32,
    KONAMI_CODE_ACCEPTED = 33,
    KONAMI_CODE_REJECTED = 34
};

/*
 * PlaceholderState — A stub state for slots not yet implemented.
 * Logs entry and immediately returns to Idle.
 */
class PlaceholderState : public State {
public:
    explicit PlaceholderState(int stateId);
    ~PlaceholderState() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIdle();

private:
    bool transitionToIdleState = false;
};

/*
 * KonamiMetaGame — Top-level state machine for Konami metagame.
 * Manages the flow of minigame launches, difficulty selection, button awards,
 * and the final Konami Code entry puzzle.
 */
class KonamiMetaGame : public StateMachine {
public:
    explicit KonamiMetaGame(Player* player);
    ~KonamiMetaGame() override;

    void populateStateMap() override;

private:
    Player* player;
};
