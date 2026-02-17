#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include <random>
#include "cli/cli-device-full.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

/*
 * Demo Playtest Simulation Suite
 *
 * Simulates "average player" behavior across multiple playthroughs
 * to validate difficulty tuning targets:
 * - Easy mode: 4-8 attempts (20-35% win rate per attempt)
 * - Hard mode: 7-10 attempts (10-15% win rate per attempt)
 *
 * Player models:
 * - Easy: 70% correct input rate, moderate reaction time
 * - Hard: 50% correct input rate, faster but less consistent
 */

class DemoPlaytestSuite : public testing::Test {
public:
    void SetUp() override {
        rng_.seed(12345);  // Fixed seed for reproducible tests
    }

    void TearDown() override {
        // Cleanup handled per-game
    }

    // Simulate button press with accuracy probability
    bool simulateInput(double accuracyRate) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng_) < accuracyRate;
    }

    // Simulate reaction time delay (ms)
    int simulateReactionTime(int minMs, int maxMs) {
        std::uniform_int_distribution<int> dist(minMs, maxMs);
        return dist(rng_);
    }

    // Simulate random choice from N options with success probability
    int simulateChoice(int numOptions, int correctIndex, double accuracyRate) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng_) < accuracyRate) {
            return correctIndex;  // Correct choice
        } else {
            // Random wrong choice
            std::uniform_int_distribution<int> wrongDist(0, numOptions - 1);
            int choice = wrongDist(rng_);
            // Ensure it's not accidentally correct
            while (choice == correctIndex && numOptions > 1) {
                choice = wrongDist(rng_);
            }
            return choice;
        }
    }

    std::mt19937 rng_;
};

// ============================================
// SIGNAL ECHO PLAYTEST SIMULATIONS
// ============================================

/*
 * Signal Echo Easy Mode Playtest
 * Target: 4-8 attempts (avg ~6)
 * Player model: 70% accuracy on sequence recall
 */
void signalEchoEasyPlaytest(DemoPlaytestSuite* suite) {
    constexpr int NUM_TRIALS = 100;
    constexpr double ACCURACY_RATE = 0.70;
    int totalAttempts = 0;
    int wins = 0;

    for (int trial = 0; trial < NUM_TRIALS; trial++) {
        DeviceInstance device = DeviceFactory::createGameDevice(0, "signal-echo");
        SimpleTimer::setPlatformClock(device.clockDriver);
        SignalEcho* game = dynamic_cast<SignalEcho*>(device.game);
        ASSERT_NE(game, nullptr);

        // Override with easy config
        game->getConfig() = SIGNAL_ECHO_EASY;
        game->resetGame();

        int attempts = 0;
        bool won = false;
        constexpr int MAX_ATTEMPTS = 20;  // Safety limit

        while (!won && attempts < MAX_ATTEMPTS) {
            attempts++;
            game->resetGame();

            // Skip to gameplay state
            State* state = game->getCurrentState();
            while (state && state->getStateId() == ECHO_INTRO) {
                device.clockDriver->advance(100);
                device.pdn->loop();
                state = game->getCurrentState();
            }

            // Simulate playing through rounds
            while (state && (state->getStateId() == ECHO_SHOW_SEQUENCE ||
                            state->getStateId() == ECHO_PLAYER_INPUT)) {

                if (state->getStateId() == ECHO_SHOW_SEQUENCE) {
                    // Wait for sequence display to complete
                    int displayTime = game->getConfig().displaySpeedMs *
                                     game->getConfig().sequenceLength + 1000;
                    device.clockDriver->advance(displayTime);
                    device.pdn->loop();
                } else if (state->getStateId() == ECHO_PLAYER_INPUT) {
                    // Simulate player input with accuracy
                    const auto& sequence = game->getSession().currentSequence;
                    int inputIndex = game->getSession().inputIndex;

                    if (inputIndex < static_cast<int>(sequence.size())) {
                        bool correctInput = sequence[inputIndex];
                        bool playerInput = suite->simulateInput(ACCURACY_RATE) ?
                                          correctInput : !correctInput;

                        // Press the appropriate button
                        if (playerInput) {
                            device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                        } else {
                            device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                        }

                        // Small delay between inputs
                        device.clockDriver->advance(suite->simulateReactionTime(200, 500));
                    }

                    device.pdn->loop();
                }

                state = game->getCurrentState();
            }

            // Check outcome
            if (state && state->getStateId() == ECHO_WIN) {
                won = true;
                wins++;
            }
        }

        totalAttempts += attempts;
        DeviceFactory::destroyDevice(device);
    }

    double avgAttempts = static_cast<double>(totalAttempts) / NUM_TRIALS;
    double winRate = static_cast<double>(wins) / NUM_TRIALS;

    // Log results
    std::cout << "\n=== Signal Echo Easy Mode Playtest ===" << std::endl;
    std::cout << "Trials: " << NUM_TRIALS << std::endl;
    std::cout << "Avg attempts to win: " << avgAttempts << std::endl;
    std::cout << "Win rate: " << (winRate * 100.0) << "%" << std::endl;
    std::cout << "Target: 4-8 attempts" << std::endl;

    // Validate against target
    EXPECT_GE(avgAttempts, 4.0) << "Easy mode too easy (< 4 attempts)";
    EXPECT_LE(avgAttempts, 8.0) << "Easy mode too hard (> 8 attempts)";
}

/*
 * Signal Echo Hard Mode Playtest
 * Target: 7-10 attempts (avg ~8-9)
 * Player model: 50% accuracy on longer sequences
 */
void signalEchoHardPlaytest(DemoPlaytestSuite* suite) {
    constexpr int NUM_TRIALS = 100;
    constexpr double ACCURACY_RATE = 0.50;
    int totalAttempts = 0;
    int wins = 0;

    for (int trial = 0; trial < NUM_TRIALS; trial++) {
        DeviceInstance device = DeviceFactory::createGameDevice(0, "signal-echo");
        SimpleTimer::setPlatformClock(device.clockDriver);
        SignalEcho* game = dynamic_cast<SignalEcho*>(device.game);
        ASSERT_NE(game, nullptr);

        // Override with hard config
        game->getConfig() = SIGNAL_ECHO_HARD;
        game->resetGame();

        int attempts = 0;
        bool won = false;
        constexpr int MAX_ATTEMPTS = 30;

        while (!won && attempts < MAX_ATTEMPTS) {
            attempts++;
            game->resetGame();

            // Skip intro
            State* state = game->getCurrentState();
            while (state && state->getStateId() == ECHO_INTRO) {
                device.clockDriver->advance(100);
                device.pdn->loop();
                state = game->getCurrentState();
            }

            // Play through game
            while (state && (state->getStateId() == ECHO_SHOW_SEQUENCE ||
                            state->getStateId() == ECHO_PLAYER_INPUT)) {

                if (state->getStateId() == ECHO_SHOW_SEQUENCE) {
                    int displayTime = game->getConfig().displaySpeedMs *
                                     game->getConfig().sequenceLength + 1000;
                    device.clockDriver->advance(displayTime);
                    device.pdn->loop();
                } else if (state->getStateId() == ECHO_PLAYER_INPUT) {
                    const auto& sequence = game->getSession().currentSequence;
                    int inputIndex = game->getSession().inputIndex;

                    if (inputIndex < static_cast<int>(sequence.size())) {
                        bool correctInput = sequence[inputIndex];
                        bool playerInput = suite->simulateInput(ACCURACY_RATE) ?
                                          correctInput : !correctInput;

                        if (playerInput) {
                            device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                        } else {
                            device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                        }

                        device.clockDriver->advance(suite->simulateReactionTime(150, 400));
                    }

                    device.pdn->loop();
                }

                state = game->getCurrentState();
            }

            if (state && state->getStateId() == ECHO_WIN) {
                won = true;
                wins++;
            }
        }

        totalAttempts += attempts;
        DeviceFactory::destroyDevice(device);
    }

    double avgAttempts = static_cast<double>(totalAttempts) / NUM_TRIALS;
    double winRate = static_cast<double>(wins) / NUM_TRIALS;

    std::cout << "\n=== Signal Echo Hard Mode Playtest ===" << std::endl;
    std::cout << "Trials: " << NUM_TRIALS << std::endl;
    std::cout << "Avg attempts to win: " << avgAttempts << std::endl;
    std::cout << "Win rate: " << (winRate * 100.0) << "%" << std::endl;
    std::cout << "Target: 7-10 attempts" << std::endl;

    EXPECT_GE(avgAttempts, 7.0) << "Hard mode too easy (< 7 attempts)";
    EXPECT_LE(avgAttempts, 10.0) << "Hard mode too hard (> 10 attempts)";
}

// ============================================
// GHOST RUNNER PLAYTEST SIMULATIONS
// ============================================
// NOTE: Tests disabled during Wave 18 redesign (#220) — Guitar Hero → Memory Maze
// TODO(#220): Rewrite Ghost Runner playtest for maze navigation mechanics
#if 0

/*
 * Ghost Runner Easy Mode Playtest
 * Target: 4-8 attempts
 * Player model: 70% success rate on timing (wide target zone helps)
 */
void ghostRunnerEasyPlaytest(DemoPlaytestSuite* suite) {
    constexpr int NUM_TRIALS = 100;
    constexpr double HIT_RATE = 0.70;  // 70% chance to hit target zone
    int totalAttempts = 0;
    int wins = 0;

    for (int trial = 0; trial < NUM_TRIALS; trial++) {
        DeviceInstance device = DeviceFactory::createGameDevice(0, "ghost-runner");
        SimpleTimer::setPlatformClock(device.clockDriver);
        GhostRunner* game = dynamic_cast<GhostRunner*>(device.game);
        ASSERT_NE(game, nullptr);

        // Override with easy config
        game->getConfig() = GHOST_RUNNER_EASY;
        game->resetGame();

        int attempts = 0;
        bool won = false;
        constexpr int MAX_ATTEMPTS = 20;

        while (!won && attempts < MAX_ATTEMPTS) {
            attempts++;
            game->resetGame();

            // Skip intro
            State* state = game->getCurrentState();
            while (state && state->getStateId() == GHOST_INTRO) {
                device.clockDriver->advance(100);
                device.pdn->loop();
                state = game->getCurrentState();
            }

            // Play through rounds
            while (state && state->getStateId() == GHOST_SHOW) {
                const auto& config = game->getConfig();

                // Simulate player decision: press in target zone or miss
                bool willHit = suite->simulateInput(HIT_RATE);

                if (willHit) {
                    // Press when ghost is in target zone
                    int targetMid = (config.targetZoneStart + config.targetZoneEnd) / 2;
                    int targetTime = targetMid * config.ghostSpeedMs;
                    device.clockDriver->advance(targetTime);
                } else {
                    // Press at random wrong time
                    int wrongTime = suite->simulateReactionTime(0, config.screenWidth * config.ghostSpeedMs);
                    // Avoid accidentally hitting target
                    while (wrongTime >= config.targetZoneStart * config.ghostSpeedMs &&
                           wrongTime <= config.targetZoneEnd * config.ghostSpeedMs) {
                        wrongTime = suite->simulateReactionTime(0, config.screenWidth * config.ghostSpeedMs);
                    }
                    device.clockDriver->advance(wrongTime);
                }

                device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                device.pdn->loop();

                // Wait for feedback
                device.clockDriver->advance(1000);
                device.pdn->loop();

                state = game->getCurrentState();
            }

            if (state && state->getStateId() == GHOST_WIN) {
                won = true;
                wins++;
            }
        }

        totalAttempts += attempts;
        DeviceFactory::destroyDevice(device);
    }

    double avgAttempts = static_cast<double>(totalAttempts) / NUM_TRIALS;
    double winRate = static_cast<double>(wins) / NUM_TRIALS;

    std::cout << "\n=== Ghost Runner Easy Mode Playtest ===" << std::endl;
    std::cout << "Trials: " << NUM_TRIALS << std::endl;
    std::cout << "Avg attempts to win: " << avgAttempts << std::endl;
    std::cout << "Win rate: " << (winRate * 100.0) << "%" << std::endl;
    std::cout << "Target: 4-8 attempts" << std::endl;

    EXPECT_GE(avgAttempts, 4.0) << "Easy mode too easy (< 4 attempts)";
    EXPECT_LE(avgAttempts, 8.0) << "Easy mode too hard (> 8 attempts)";
}

/*
 * Ghost Runner Hard Mode Playtest
 * Target: 7-10 attempts
 * Player model: 50% hit rate (narrow zone, faster speed)
 */
void ghostRunnerHardPlaytest(DemoPlaytestSuite* suite) {
    constexpr int NUM_TRIALS = 100;
    constexpr double HIT_RATE = 0.50;
    int totalAttempts = 0;
    int wins = 0;

    for (int trial = 0; trial < NUM_TRIALS; trial++) {
        DeviceInstance device = DeviceFactory::createGameDevice(0, "ghost-runner");
        SimpleTimer::setPlatformClock(device.clockDriver);
        GhostRunner* game = dynamic_cast<GhostRunner*>(device.game);
        ASSERT_NE(game, nullptr);

        game->getConfig() = GHOST_RUNNER_HARD;
        game->resetGame();

        int attempts = 0;
        bool won = false;
        constexpr int MAX_ATTEMPTS = 30;

        while (!won && attempts < MAX_ATTEMPTS) {
            attempts++;
            game->resetGame();

            State* state = game->getCurrentState();
            while (state && state->getStateId() == GHOST_INTRO) {
                device.clockDriver->advance(100);
                device.pdn->loop();
                state = game->getCurrentState();
            }

            while (state && state->getStateId() == GHOST_SHOW) {
                const auto& config = game->getConfig();
                bool willHit = suite->simulateInput(HIT_RATE);

                if (willHit) {
                    int targetMid = (config.targetZoneStart + config.targetZoneEnd) / 2;
                    int targetTime = targetMid * config.ghostSpeedMs;
                    device.clockDriver->advance(targetTime);
                } else {
                    int wrongTime = suite->simulateReactionTime(0, config.screenWidth * config.ghostSpeedMs);
                    while (wrongTime >= config.targetZoneStart * config.ghostSpeedMs &&
                           wrongTime <= config.targetZoneEnd * config.ghostSpeedMs) {
                        wrongTime = suite->simulateReactionTime(0, config.screenWidth * config.ghostSpeedMs);
                    }
                    device.clockDriver->advance(wrongTime);
                }

                device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                device.pdn->loop();
                device.clockDriver->advance(1000);
                device.pdn->loop();
                state = game->getCurrentState();
            }

            if (state && state->getStateId() == GHOST_WIN) {
                won = true;
                wins++;
            }
        }

        totalAttempts += attempts;
        DeviceFactory::destroyDevice(device);
    }

    double avgAttempts = static_cast<double>(totalAttempts) / NUM_TRIALS;
    double winRate = static_cast<double>(wins) / NUM_TRIALS;

    std::cout << "\n=== Ghost Runner Hard Mode Playtest ===" << std::endl;
    std::cout << "Trials: " << NUM_TRIALS << std::endl;
    std::cout << "Avg attempts to win: " << avgAttempts << std::endl;
    std::cout << "Win rate: " << (winRate * 100.0) << "%" << std::endl;
    std::cout << "Target: 7-10 attempts" << std::endl;

    EXPECT_GE(avgAttempts, 7.0) << "Hard mode too easy (< 7 attempts)";
    EXPECT_LE(avgAttempts, 10.0) << "Hard mode too hard (> 10 attempts)";
}

#endif // Ghost Runner playtest tests disabled

// ============================================
// SUMMARY PLAYTEST (ALL GAMES)
// ============================================

/*
 * Run playtests for all implemented games and report summary stats.
 * This test is informational - it doesn't fail, just reports data.
 */
void demoPlaytestSummary(DemoPlaytestSuite* suite) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "DEMO MODE PLAYTEST SUMMARY" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "\nRunning simulated playtests for all FDN games..." << std::endl;
    std::cout << "Each test runs 100 trials with simulated player behavior." << std::endl;
    std::cout << "\nTarget difficulty:" << std::endl;
    std::cout << "  Easy: 4-8 attempts (20-35% win rate)" << std::endl;
    std::cout << "  Hard: 7-10 attempts (10-15% win rate)" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    // Run all game playtests
    signalEchoEasyPlaytest(suite);
    signalEchoHardPlaytest(suite);
    // Ghost Runner playtests disabled — Wave 18 redesign (#220) changed to maze
    // ghostRunnerEasyPlaytest(suite);
    // ghostRunnerHardPlaytest(suite);

    // Note: Other games (Spike Vector, Firewall Decrypt, Cipher Path,
    // Exploit Sequencer, Breach Defense) would have playtests added here
    // once their difficulty configs are finalized.

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Playtest complete. See results above." << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}

#endif // NATIVE_BUILD
