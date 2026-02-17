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
    KMG_HANDSHAKE = 0,
    KMG_EASY_LAUNCH_START = 1,
    KMG_EASY_LAUNCH_END = 7,
    KMG_REPLAY_EASY_START = 8,
    KMG_REPLAY_EASY_END = 14,
    KMG_HARD_LAUNCH_START = 15,
    KMG_HARD_LAUNCH_END = 21,
    KMG_MASTERY_REPLAY_START = 22,
    KMG_MASTERY_REPLAY_END = 28,
    KMG_BUTTON_AWARDED = 29,
    KMG_BOON_AWARDED = 30,
    KMG_GAME_OVER_RETURN = 31,
    KMG_CODE_ENTRY = 32,
    KMG_CODE_ACCEPTED = 33,
    KMG_CODE_REJECTED = 34
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
    void onStateLoop(Device* PDN) override;

private:
    Player* player;
    void wireTransitions();
};
