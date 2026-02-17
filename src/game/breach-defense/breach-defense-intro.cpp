#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-resources.hpp"

static const char* TAG = "BreachDefenseIntro";

BreachDefenseIntro::BreachDefenseIntro(BreachDefense* game) :
    BaseIntroState<BreachDefense>(
        BREACH_INTRO,
        game,
        Config{
            TAG,
            "BREACH DEFENSE",
            10,
            20,
            "Hold the line.",
            10,
            45,
            AnimationConfig{
                AnimationType::IDLE,
                true,
                2,
                EaseCurve::EASE_IN_OUT,
                LEDState(),
                0
            },
            2000
        }
    )
{
}
