#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-resources.hpp"

static const char* TAG = "BreachDefenseWin";

BreachDefenseWin::BreachDefenseWin(BreachDefense* game) :
    BaseWinState<BreachDefense>(
        BREACH_WIN,
        game,
        Config{
            TAG,
            "BREACH BLOCKED",
            10,
            30,
            false,
            30,
            50,
            AnimationConfig{
                AnimationType::VERTICAL_CHASE,
                true,
                5,
                EaseCurve::EASE_IN_OUT,
                BREACH_DEFENSE_WIN_STATE,
                500
            },
            200,
            3000,
            [game]() {
                auto& cfg = game->getConfig();
                return (cfg.numLanes >= 5 && cfg.missesAllowed <= 1);
            },
            nullptr
        }
    )
{
}
