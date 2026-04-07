#pragma once

#include "animation-base.hpp"
#include "utils/simple-timer.hpp"
#include "apps/main-menu/main-menu-resources.hpp"
#include <algorithm>
#include <cstdlib>

// Reuses the MainMenuAnimation visual style (sparkle recess + sweeping fin).
// Brightness is read from config_.initialState so the caller controls it via
// the AnimationConfig rather than hardcoded constants.
// Animation speed is driven entirely by config_.speed (frame interval in ms),
// so the caller maps intensity → speed to get both effects from one value.
class PlayerDetectedAnimation : public AnimationBase {
public:
    PlayerDetectedAnimation() : AnimationBase() {}

protected:
    void onInit() override {
        brightness_   = config_.initialState.recessLights[0].brightness;
        currentState_ = config_.initialState;
        finOffset_    = 0;
        finState_     = FinState::SCROLLING;

        const uint16_t stagger = 511 / MAX_SPARKLES;
        for (uint8_t i = 0; i < MAX_SPARKLES; i++) {
            sparkles_[i].ledIdx    = rand() % NUM_RECESS_LIGHTS;
            sparkles_[i].targetIdx = rand() % NUM_RECESS_LIGHTS;
            sparkles_[i].progress  = i * stagger;
        }
    }

    LEDState onAnimate() override {
        animateRecessLights();
        animateFinLights();
        return currentState_;
    }

private:
    void animateRecessLights() {
        for (uint8_t i = 0; i < NUM_RECESS_LIGHTS; i++) {
            currentState_.setRecessLight(i, mainMenuRecessColors[i], brightness_);
        }

        for (auto& s : sparkles_) {
            s.progress += SPARKLE_STEP;

            if (s.progress >= 511) {
                s.ledIdx    = rand() % NUM_RECESS_LIGHTS;
                s.targetIdx = rand() % NUM_RECESS_LIGHTS;
                s.progress  = 0;
            }

            uint8_t rawT = (s.progress <= 255)
                ? static_cast<uint8_t>(s.progress)
                : static_cast<uint8_t>(511 - s.progress);
            uint8_t easedT = getEasingValue(rawT, EaseCurve::EASE_IN_OUT);

            const LEDColor& base = mainMenuRecessColors[s.ledIdx];
            const LEDColor& peak = mainMenuRecessColors[s.targetIdx];
            currentState_.setRecessLight(s.ledIdx, interpolateColor(base, peak, easedT), brightness_);
        }
    }

    void animateFinLights() {
        if (finState_ == FinState::PAUSING) {
            if (finPauseTimer_.expired()) {
                finState_  = FinState::SCROLLING;
                finOffset_ = 0;
            }
            return;
        }

        finOffset_ = std::min(static_cast<uint16_t>(finOffset_ + FIN_STEP), FIN_CYCLE);

        for (uint8_t i = 0; i < NUM_FIN_LIGHTS; i++) {
            uint16_t virtualPos = static_cast<uint16_t>(i * 256) + finOffset_;
            uint8_t  paletteIdx = virtualPos / 256;
            uint8_t  fraction   = virtualPos % 256;

            LEDColor color;
            if (paletteIdx < NUM_FIN_PALETTE) {
                const LEDColor& from = mainMenuFinColors[paletteIdx];
                const LEDColor  to   = (paletteIdx + 1 < NUM_FIN_PALETTE)
                    ? mainMenuFinColors[paletteIdx + 1]
                    : LEDColor(0, 0, 0);
                color = interpolateColor(from, to, fraction);
            } else {
                color = LEDColor(0, 0, 0);
            }
            currentState_.setFinLight(i, color, brightness_);
        }

        if (finOffset_ >= FIN_CYCLE) {
            finState_ = FinState::PAUSING;
            finPauseTimer_.setTimer(FIN_PAUSE_MS);
        }
    }

    static constexpr uint8_t  NUM_FIN_LIGHTS    = 9;
    static constexpr uint8_t  NUM_FIN_PALETTE   = 13;
    static constexpr uint8_t  NUM_RECESS_LIGHTS = 23;
    static constexpr uint8_t  SPARKLE_STEP      = 8;
    static constexpr uint8_t  MAX_SPARKLES      = NUM_RECESS_LIGHTS / 4;
    static constexpr uint8_t  FIN_STEP          = 50;
    static constexpr uint16_t FIN_CYCLE         = NUM_FIN_PALETTE * 256;
    static constexpr uint16_t FIN_PAUSE_MS      = 2000;

    enum class FinState { SCROLLING, PAUSING };

    struct Sparkle {
        uint8_t  ledIdx    = 0;
        uint8_t  targetIdx = 0;
        uint16_t progress  = 0;
    };

    uint8_t     brightness_ = 87;
    Sparkle     sparkles_[MAX_SPARKLES];
    uint16_t    finOffset_ = 0;
    FinState    finState_  = FinState::SCROLLING;
    SimpleTimer finPauseTimer_;
};
