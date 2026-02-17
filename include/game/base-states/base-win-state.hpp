#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"
#include "device/drivers/light-interface.hpp"
#include "device/drivers/display.hpp"
#include "game/minigame.hpp"
#include <functional>
#include <string>

/*
 * BaseWinState — Reusable template for minigame win screens.
 * Provides common functionality:
 * - Victory message display
 * - Score display (optional)
 * - Hard mode detection
 * - MiniGameOutcome setting
 * - LED animation
 * - Haptic feedback
 * - Managed mode vs standalone mode handling
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
class BaseWinState : public State {
public:
    struct Config {
        const char* logTag;
        const char* message;
        int messageX;
        int messageY;
        bool showScore = false;
        int scoreX = 30;
        int scoreY = 50;
        AnimationConfig ledAnimation;
        int hapticIntensity = 200;
        int displayDurationMs = 3000;
        std::function<bool()> hardModeDetector;
        std::function<void(Device*)> customRenderer = nullptr;
    };

    BaseWinState(int stateId, GameType* game, const Config& config) :
        State(stateId),
        game(game),
        config(config)
    {
    }

    virtual ~BaseWinState() {
        game = nullptr;
    }

    void onStateMounted(Device* PDN) override {
        transitionFlag = false;

        auto& gameConfig = game->getConfig();
        auto& session = game->getSession();

        // Determine hard mode
        bool isHard = config.hardModeDetector ? config.hardModeDetector() : false;

        MiniGameOutcome winOutcome;
        winOutcome.result = MiniGameResult::WON;
        winOutcome.score = session.score;
        winOutcome.hardMode = isHard;
        game->setOutcome(winOutcome);

        LOG_I(config.logTag, "%s — score %d, hard %d", config.message, session.score, isHard);

        // Custom rendering if provided, otherwise use default
        if (config.customRenderer) {
            config.customRenderer(PDN);
        } else {
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
                ->drawText(config.message, config.messageX, config.messageY);

            if (config.showScore) {
                std::string scoreStr = "Score: " + std::to_string(session.score);
                PDN->getDisplay()->drawText(scoreStr.c_str(), config.scoreX, config.scoreY);
            }

            PDN->getDisplay()->render();
        }

        // LED animation
        PDN->getLightManager()->startAnimation(config.ledAnimation);

        // Haptic feedback
        PDN->getHaptics()->setIntensity(config.hapticIntensity);

        winTimer.setTimer(config.displayDurationMs);
    }

    void onStateLoop(Device* PDN) override {
        if (winTimer.expired()) {
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
        winTimer.invalidate();
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
    SimpleTimer winTimer;
    bool transitionFlag = false;
};
