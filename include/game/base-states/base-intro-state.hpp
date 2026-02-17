#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/platform-clock.hpp"
#include "device/device.hpp"
#include "device/drivers/light-interface.hpp"
#include "device/drivers/display.hpp"
#include <functional>

/*
 * BaseIntroState — Reusable template for minigame intro screens.
 * Provides common functionality:
 * - Title and subtitle text display
 * - Session reset and game initialization
 * - LED idle animation
 * - Timed transition to next state
 *
 * Template Parameters:
 *   GameType: The minigame class (e.g., BreachDefense, GhostRunner)
 *
 * Game class requirements:
 *   - getSession() — returns session object with reset() method
 *   - resetGame() — resets game-specific state
 *   - setStartTime(uint32_t) — sets game start timestamp
 */
template<typename GameType>
class BaseIntroState : public State {
public:
    struct Config {
        const char* logTag;
        const char* title;
        int titleX;
        int titleY;
        const char* subtitle;
        int subtitleX;
        int subtitleY;
        AnimationConfig ledAnimation;
        int introDurationMs = 2000;
        std::function<void(Device*)> customMounted = nullptr;
        std::function<void(Device*)> customLoop = nullptr;
    };

    BaseIntroState(int stateId, GameType* game, const Config& config) :
        State(stateId),
        game(game),
        config(config)
    {
    }

    virtual ~BaseIntroState() {
        game = nullptr;
    }

    void onStateMounted(Device* PDN) override {
        transitionFlag = false;

        LOG_I(config.logTag, "%s intro", config.title);

        // Reset session for a fresh game
        game->getSession().reset();
        game->resetGame();

        PlatformClock* clock = SimpleTimer::getPlatformClock();
        game->setStartTime(clock != nullptr ? clock->milliseconds() : 0);

        // Custom mounted logic if provided
        if (config.customMounted) {
            config.customMounted(PDN);
        } else {
            // Default display title screen
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
                ->drawText(config.title, config.titleX, config.titleY);

            if (config.subtitle != nullptr) {
                PDN->getDisplay()->drawText(config.subtitle, config.subtitleX, config.subtitleY);
            }

            PDN->getDisplay()->render();
        }

        // LED idle animation
        PDN->getLightManager()->startAnimation(config.ledAnimation);

        // Start intro timer
        introTimer.setTimer(config.introDurationMs);
    }

    void onStateLoop(Device* PDN) override {
        // Custom loop logic if provided
        if (config.customLoop) {
            config.customLoop(PDN);
        }

        if (introTimer.expired()) {
            transitionFlag = true;
        }
    }

    void onStateDismounted(Device* PDN) override {
        introTimer.invalidate();
        transitionFlag = false;
    }

    bool shouldTransition() const {
        return transitionFlag;
    }

protected:
    GameType* game;
    Config config;
    SimpleTimer introTimer;
    bool transitionFlag = false;
};
