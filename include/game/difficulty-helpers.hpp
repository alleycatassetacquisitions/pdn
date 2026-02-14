#pragma once

#include "game/difficulty-scaler.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include <algorithm>

/*
 * Helper functions for applying difficulty scaling to minigame configs.
 *
 * Each game has an easy and hard config with specific parameter bounds.
 * The scaler provides a 0.0-1.0 value that interpolates between these bounds.
 *
 * Usage:
 * float scale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
 * SignalEchoConfig config = makeScaledSignalEchoConfig(scale, managedMode);
 */

// Linear interpolation helper
template<typename T>
inline T lerp(T a, T b, float t) {
    return static_cast<T>(a + (b - a) * t);
}

/*
 * Signal Echo scaled config
 * Easy: length=4, sequences=4, speed=600ms, mistakes=3
 * Hard: length=8, sequences=4, speed=400ms, mistakes=1
 */
inline SignalEchoConfig makeScaledSignalEchoConfig(float scale, bool managedMode = false) {
    const auto& easy = SIGNAL_ECHO_EASY;
    const auto& hard = SIGNAL_ECHO_HARD;

    SignalEchoConfig config;
    config.sequenceLength = lerp(easy.sequenceLength, hard.sequenceLength, scale);
    config.numSequences = easy.numSequences;  // constant at 4
    config.displaySpeedMs = lerp(easy.displaySpeedMs, hard.displaySpeedMs, scale);
    config.timeLimitMs = 0;  // no time limit
    config.cumulative = false;
    config.allowedMistakes = lerp(easy.allowedMistakes, hard.allowedMistakes, scale);
    config.rngSeed = 0;
    config.managedMode = managedMode;

    return config;
}

/*
 * Ghost Runner scaled config
 * Easy: speed=50ms, zone=35-65 (30 wide), rounds=4, misses=3
 * Hard: speed=30ms, zone=42-58 (16 wide), rounds=6, misses=1
 */
inline GhostRunnerConfig makeScaledGhostRunnerConfig(float scale, bool managedMode = false) {
    const auto& easy = GHOST_RUNNER_EASY;
    const auto& hard = GHOST_RUNNER_HARD;

    GhostRunnerConfig config;
    config.ghostSpeedMs = lerp(easy.ghostSpeedMs, hard.ghostSpeedMs, scale);
    config.screenWidth = 100;  // constant
    config.targetZoneStart = lerp(easy.targetZoneStart, hard.targetZoneStart, scale);
    config.targetZoneEnd = lerp(easy.targetZoneEnd, hard.targetZoneEnd, scale);
    config.rounds = lerp(easy.rounds, hard.rounds, scale);
    config.missesAllowed = lerp(easy.missesAllowed, hard.missesAllowed, scale);
    config.rngSeed = 0;
    config.managedMode = managedMode;

    return config;
}

/*
 * Spike Vector scaled config
 * Easy: speed=40ms, positions=5, waves=5, hits=3
 * Hard: speed=20ms, positions=7, waves=8, hits=1
 */
inline SpikeVectorConfig makeScaledSpikeVectorConfig(float scale, bool managedMode = false) {
    const auto& easy = SPIKE_VECTOR_EASY;
    const auto& hard = SPIKE_VECTOR_HARD;

    SpikeVectorConfig config;
    config.approachSpeedMs = lerp(easy.approachSpeedMs, hard.approachSpeedMs, scale);
    config.trackLength = 100;  // constant
    config.numPositions = lerp(easy.numPositions, hard.numPositions, scale);
    config.startPosition = (config.numPositions / 2);  // always middle
    config.waves = lerp(easy.waves, hard.waves, scale);
    config.hitsAllowed = lerp(easy.hitsAllowed, hard.hitsAllowed, scale);
    config.rngSeed = 0;
    config.managedMode = managedMode;

    return config;
}

/*
 * Firewall Decrypt scaled config
 * Easy: candidates=5, rounds=3, similarity=0.2, no timer
 * Hard: candidates=10, rounds=4, similarity=0.7, 15s timer
 */
inline FirewallDecryptConfig makeScaledFirewallDecryptConfig(float scale, bool managedMode = false) {
    const auto& easy = FIREWALL_DECRYPT_EASY;
    const auto& hard = FIREWALL_DECRYPT_HARD;

    FirewallDecryptConfig config;
    config.numCandidates = lerp(easy.numCandidates, hard.numCandidates, scale);
    config.numRounds = lerp(easy.numRounds, hard.numRounds, scale);
    config.similarity = easy.similarity + (hard.similarity - easy.similarity) * scale;
    config.timeLimitMs = (scale > 0.5f) ? lerp(0, hard.timeLimitMs, (scale - 0.5f) * 2.0f) : 0;
    config.rngSeed = 0;
    config.managedMode = managedMode;

    return config;
}

/*
 * Cipher Path scaled config
 * Easy: size=6, budget=12, rounds=2
 * Hard: size=10, budget=14, rounds=4
 */
inline CipherPathConfig makeScaledCipherPathConfig(float scale, bool managedMode = false) {
    const auto& easy = CIPHER_PATH_EASY;
    const auto& hard = CIPHER_PATH_HARD;

    CipherPathConfig config;
    config.gridSize = lerp(easy.gridSize, hard.gridSize, scale);
    config.moveBudget = lerp(easy.moveBudget, hard.moveBudget, scale);
    config.rounds = lerp(easy.rounds, hard.rounds, scale);
    config.rngSeed = 0;
    config.managedMode = managedMode;

    return config;
}

/*
 * Exploit Sequencer scaled config
 * Easy: speed=40ms, window=15, exploits=2, sequences=2, fails=3
 * Hard: speed=25ms, window=6, exploits=4, sequences=4, fails=1
 */
inline ExploitSequencerConfig makeScaledExploitSequencerConfig(float scale, bool managedMode = false) {
    const auto& easy = EXPLOIT_SEQUENCER_EASY;
    const auto& hard = EXPLOIT_SEQUENCER_HARD;

    ExploitSequencerConfig config;
    config.scrollSpeedMs = lerp(easy.scrollSpeedMs, hard.scrollSpeedMs, scale);
    config.scrollLength = 100;  // constant
    config.markerPosition = 50;  // constant
    config.timingWindow = lerp(easy.timingWindow, hard.timingWindow, scale);
    config.exploitsPerSeq = lerp(easy.exploitsPerSeq, hard.exploitsPerSeq, scale);
    config.sequences = lerp(easy.sequences, hard.sequences, scale);
    config.failsAllowed = lerp(easy.failsAllowed, hard.failsAllowed, scale);
    config.rngSeed = 0;
    config.managedMode = managedMode;

    return config;
}

/*
 * Breach Defense scaled config
 * Easy: lanes=3, speed=40ms, threats=6, misses=3
 * Hard: lanes=5, speed=20ms, threats=12, misses=1
 */
inline BreachDefenseConfig makeScaledBreachDefenseConfig(float scale, bool managedMode = false) {
    const auto& easy = BREACH_DEFENSE_EASY;
    const auto& hard = BREACH_DEFENSE_HARD;

    BreachDefenseConfig config;
    config.numLanes = lerp(easy.numLanes, hard.numLanes, scale);
    config.threatSpeedMs = lerp(easy.threatSpeedMs, hard.threatSpeedMs, scale);
    config.threatDistance = 100;  // constant
    config.totalThreats = lerp(easy.totalThreats, hard.totalThreats, scale);
    config.missesAllowed = lerp(easy.missesAllowed, hard.missesAllowed, scale);
    config.rngSeed = 0;
    config.managedMode = managedMode;

    return config;
}

/*
 * Apply scaled difficulty to a minigame based on GameType.
 * This is the main entry point for configuring games with auto-scaling.
 *
 * @param gameType Which game to configure
 * @param game Pointer to the MiniGame instance
 * @param scaler The difficulty scaler (tracks performance)
 * @param hardMode Whether the player selected hard mode (scales within 0.5-1.0 if true)
 * @param managedMode Whether the game is in managed mode (FDN)
 */
inline void applyScaledDifficulty(
    GameType gameType,
    MiniGame* game,
    DifficultyScaler& scaler,
    bool hardMode,
    bool managedMode
) {
    if (!game) return;

    // Get base scale from scaler (0.0-1.0)
    float baseScale = scaler.getScaledDifficulty(gameType);

    // Map to the appropriate difficulty band
    // Easy mode: 0.0-0.5 range
    // Hard mode: 0.5-1.0 range
    float scale = hardMode ? (0.5f + baseScale * 0.5f) : (baseScale * 0.5f);

    switch (gameType) {
        case GameType::SIGNAL_ECHO: {
            auto* echo = static_cast<SignalEcho*>(game);
            echo->getConfig() = makeScaledSignalEchoConfig(scale, managedMode);
            break;
        }
        case GameType::GHOST_RUNNER: {
            auto* gr = static_cast<GhostRunner*>(game);
            gr->getConfig() = makeScaledGhostRunnerConfig(scale, managedMode);
            break;
        }
        case GameType::SPIKE_VECTOR: {
            auto* sv = static_cast<SpikeVector*>(game);
            sv->getConfig() = makeScaledSpikeVectorConfig(scale, managedMode);
            break;
        }
        case GameType::FIREWALL_DECRYPT: {
            auto* fw = static_cast<FirewallDecrypt*>(game);
            fw->getConfig() = makeScaledFirewallDecryptConfig(scale, managedMode);
            break;
        }
        case GameType::CIPHER_PATH: {
            auto* cp = static_cast<CipherPath*>(game);
            cp->getConfig() = makeScaledCipherPathConfig(scale, managedMode);
            break;
        }
        case GameType::EXPLOIT_SEQUENCER: {
            auto* es = static_cast<ExploitSequencer*>(game);
            es->getConfig() = makeScaledExploitSequencerConfig(scale, managedMode);
            break;
        }
        case GameType::BREACH_DEFENSE: {
            auto* bd = static_cast<BreachDefense*>(game);
            bd->getConfig() = makeScaledBreachDefenseConfig(scale, managedMode);
            break;
        }
        default:
            break;
    }
}
