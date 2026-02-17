#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"
#include "device/drivers/light-interface.hpp"
#include "device/drivers/display.hpp"
#include "game/minigame.hpp"
#include <functional>

/*
 * BaseLoseState — Reusable template for minigame lose screens.
 * Provides common functionality:
 * - Defeat message display
 * - MiniGameOutcome setting
 * - LED animation
 * - Haptic feedback
 * - Managed mode vs standalone mode handling
 * - Optional custom rendering and animation logic
 *
 * Template Parameters:
 *   GameType: The minigame class (e.g., BreachDefense, GhostRunner)
 *
 * Game class requirements:
 *   - getConfig() — returns config object with managedMode field
 *   - getSession() — returns session object with score field
 *   - setOutcome(const MiniGameOutcome&) — sets the game outcome
 */
template<typename GameType>
class BaseLoseState : public State {
public:
    struct Config {
        const char* logTag;
        const char* message;
        int messageX;
        int messageY;
        AnimationConfig ledAnimation;
        int hapticIntensity = 255;
        int displayDurationMs = 3000;
        std::function<void(Device*)> customRenderer = nullptr;
        std::function<void(Device*, SimpleTimer&)> customLoop = nullptr;
    };

    BaseLoseState(int stateId, GameType* game, const Config& config) :
        State(stateId),
        game(game),
        config(config)
    {
    }

    virtual ~BaseLoseState() {
        game = nullptr;
    }

    void onStateMounted(Device* PDN) override {
        transitionFlag = false;

        auto& session = game->getSession();

        MiniGameOutcome loseOutcome;
        loseOutcome.result = MiniGameResult::LOST;
        loseOutcome.score = session.score;
        loseOutcome.hardMode = false;
        game->setOutcome(loseOutcome);

        LOG_I(config.logTag, "%s — score %d", config.message, session.score);

        // Custom rendering if provided, otherwise use default
        if (config.customRenderer) {
            config.customRenderer(PDN);
        } else {
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
                ->drawText(config.message, config.messageX, config.messageY);
            PDN->getDisplay()->render();
        }

        // LED animation
        PDN->getLightManager()->startAnimation(config.ledAnimation);

        // Haptic feedback
        PDN->getHaptics()->setIntensity(config.hapticIntensity);

        loseTimer.setTimer(config.displayDurationMs);
    }

    void onStateLoop(Device* PDN) override {
        // Custom loop logic if provided (e.g., for flashing animations)
        if (config.customLoop) {
            config.customLoop(PDN, loseTimer);
        }

        if (loseTimer.expired()) {
            PDN->getHaptics()->off();
            if (!game->getConfig().managedMode) {
                // In standalone mode, restart the game
                transitionFlag = true;
            } else {
                // In managed mode, return to the previous app (e.g. Quickdraw)
                PDN->returnToPreviousApp();
            }
        }
    }

    void onStateDismounted(Device* PDN) override {
        loseTimer.invalidate();
        transitionFlag = false;
        PDN->getHaptics()->off();
    }

    bool shouldTransition() const {
        return transitionFlag;
    }

    bool isTerminalState() const override {
        return game->getConfig().managedMode;
    }

protected:
    GameType* game;
    Config config;
    SimpleTimer loseTimer;
    bool transitionFlag = false;
};
