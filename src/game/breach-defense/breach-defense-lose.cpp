#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-resources.hpp"

static const char* TAG = "BreachDefenseLose";

BreachDefenseLose::BreachDefenseLose(BreachDefense* game) :
    BaseLoseState<BreachDefense>(
        BREACH_LOSE,
        game,
        Config{
            TAG,
            "BREACH OPEN",
            10,
            30,
            AnimationConfig{
                AnimationType::IDLE,
                true,
                8,
                EaseCurve::LINEAR,
                BREACH_DEFENSE_LOSE_STATE,
                0
            },
            255,
            3000,
            nullptr,
            nullptr
        }
    )
{
}
