#pragma once

#include "state/state-machine.hpp"
#include "game/player.hpp"

/*
 * KonamiMetaGame - Master progression system for FDN minigames.
 *
 * Flow:
 * 1. Player encounters FDN -> triggers app switch from Quickdraw
 * 2. First encounter: Easy mode -> Win -> Button awarded -> konamiProgress updated
 * 3. Button replay: No new rewards
 * 4. All 7 buttons -> Konami code entry -> 13 inputs -> hard mode unlocked
 * 5. Hard mode -> Win -> Boon awarded -> colorProfileEligibility updated
 * 6. Mastery replay -> Mode select -> Launch game with correct difficulty
 */

constexpr int KONAMI_METAGAME_APP_ID = 9;

class KonamiMetaGame : public StateMachine {
public:
    explicit KonamiMetaGame(Player* player);
    ~KonamiMetaGame() override = default;

    void populateStateMap() override;

private:
    Player* player;
};
