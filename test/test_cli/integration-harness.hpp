#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include <vector>
#include <map>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/minigame.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

/**
 * ============================================
 * MULTI-PLAYER INTEGRATION TEST HARNESS
 * ============================================
 *
 * Test infrastructure for simulating full FDN demo scenarios with
 * multiple players (hunters + bounties) and NPCs (one per game type).
 *
 * Capabilities:
 * - Create up to MAX_DEVICES (11) simultaneous devices
 * - Configure N players (hunters/bounties) + M NPCs
 * - Simulate serial cable connections
 * - Coordinate multi-device state progression
 * - Verify konami progress across multiple encounters
 * - Test cable disconnect/recovery scenarios
 *
 * Usage:
 *   MultiPlayerHarness harness;
 *   harness.addPlayer(true);   // Add hunter
 *   harness.addPlayer(false);  // Add bounty
 *   harness.addNpc(GameType::SIGNAL_ECHO);
 *   harness.setup();
 *   harness.connectCable(0, 2);  // Player 0 to NPC 2
 *   harness.tick(10);
 */
class MultiPlayerHarness {
public:
    MultiPlayerHarness() = default;

    ~MultiPlayerHarness() {
        cleanup();
    }

    // ========================================
    // CONFIGURATION
    // ========================================

    /**
     * Add a player device (hunter or bounty).
     * Call before setup().
     */
    void addPlayer(bool isHunter) {
        PlayerConfig config;
        config.deviceIndex = static_cast<int>(players_.size() + npcs_.size());
        config.isHunter = isHunter;
        players_.push_back(config);
    }

    /**
     * Add an FDN NPC device for the specified game type.
     * Call before setup().
     */
    void addNpc(GameType gameType) {
        NpcConfig config;
        config.deviceIndex = static_cast<int>(players_.size() + npcs_.size());
        config.gameType = gameType;
        npcs_.push_back(config);
    }

    /**
     * Initialize all configured devices.
     * Must be called after addPlayer/addNpc and before any other operations.
     */
    void setup() {
        // Reset singleton state
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        // Create player devices
        for (auto& config : players_) {
            DeviceInstance device = DeviceFactory::createDevice(config.deviceIndex, config.isHunter);
            devices_.push_back(device);
        }

        // Create NPC devices
        for (auto& config : npcs_) {
            DeviceInstance device = DeviceFactory::createFdnDevice(config.deviceIndex, config.gameType);
            devices_.push_back(device);
        }

        // Set platform clock to first device's clock (all devices share it in tests)
        if (!devices_.empty()) {
            SimpleTimer::setPlatformClock(devices_[0].clockDriver);
        }
    }

    /**
     * Clean up all devices and reset singleton state.
     */
    void cleanup() {
        for (auto& device : devices_) {
            DeviceFactory::destroyDevice(device);
        }
        devices_.clear();
        players_.clear();
        npcs_.clear();

        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    // ========================================
    // DEVICE ACCESS
    // ========================================

    /**
     * Get device by index (0 = first player, 1 = second player, etc.)
     */
    DeviceInstance& getDevice(int index) {
        return devices_.at(index);
    }

    /**
     * Get first player device (convenience accessor)
     */
    DeviceInstance& getPlayer(int playerIndex = 0) {
        if (playerIndex >= static_cast<int>(players_.size())) {
            throw std::runtime_error("Player index out of range");
        }
        return devices_.at(playerIndex);
    }

    /**
     * Get NPC device by index (0 = first NPC, 1 = second NPC, etc.)
     */
    DeviceInstance& getNpc(int npcIndex = 0) {
        int deviceIndex = static_cast<int>(players_.size()) + npcIndex;
        if (deviceIndex >= static_cast<int>(devices_.size())) {
            throw std::runtime_error("NPC index out of range");
        }
        return devices_.at(deviceIndex);
    }

    /**
     * Get number of player devices
     */
    int getPlayerCount() const {
        return static_cast<int>(players_.size());
    }

    /**
     * Get number of NPC devices
     */
    int getNpcCount() const {
        return static_cast<int>(npcs_.size());
    }

    // ========================================
    // SIMULATION CONTROL
    // ========================================

    /**
     * Execute N ticks across all devices.
     * Transfers serial data and loops all device state machines.
     */
    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            for (auto& device : devices_) {
                device.pdn->loop();
            }
        }
    }

    /**
     * Execute N ticks with time advancement.
     * Advances all device clocks by delayMs per tick.
     */
    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            // Advance all clocks
            for (auto& device : devices_) {
                device.clockDriver->advance(delayMs);
            }
            // Transfer serial data and loop
            SerialCableBroker::getInstance().transferData();
            for (auto& device : devices_) {
                device.pdn->loop();
            }
        }
    }

    /**
     * Advance specific player to Idle state (skip startup sequence).
     */
    void advancePlayerToIdle(int playerIndex) {
        DeviceInstance& player = getPlayer(playerIndex);
        // stateMap index 6 = Idle (after removing AllegiancePickerState)
        player.game->skipToState(player.pdn, 6);
        player.pdn->loop();
    }

    /**
     * Advance all players to Idle state.
     */
    void advanceAllPlayersToIdle() {
        for (int i = 0; i < getPlayerCount(); i++) {
            advancePlayerToIdle(i);
        }
    }

    // ========================================
    // CABLE MANAGEMENT
    // ========================================

    /**
     * Connect serial cable between two devices.
     * @param deviceA Index of first device
     * @param deviceB Index of second device
     */
    void connectCable(int deviceA, int deviceB) {
        SerialCableBroker::getInstance().connect(deviceA, deviceB);
    }

    /**
     * Disconnect serial cable between two devices.
     * @param deviceA Index of first device
     * @param deviceB Index of second device
     */
    void disconnectCable(int deviceA, int deviceB) {
        SerialCableBroker::getInstance().disconnect(deviceA, deviceB);
    }

    /**
     * Check if two devices are connected by cable.
     */
    bool isCableConnected(int deviceA, int deviceB) {
        int connected = SerialCableBroker::getInstance().getConnectedDevice(deviceA);
        return connected == deviceB;
    }

    // ========================================
    // STATE QUERIES
    // ========================================

    /**
     * Get current Quickdraw state ID for a player.
     */
    int getPlayerStateId(int playerIndex) {
        DeviceInstance& player = getPlayer(playerIndex);
        return player.game->getCurrentStateId();
    }

    /**
     * Get current game state ID for an NPC.
     */
    int getNpcStateId(int npcIndex) {
        DeviceInstance& npc = getNpc(npcIndex);
        return npc.game->getCurrentStateId();
    }

    /**
     * Check if player is in expected state.
     */
    bool isPlayerInState(int playerIndex, int expectedStateId) {
        return getPlayerStateId(playerIndex) == expectedStateId;
    }

    // ========================================
    // PROGRESSION QUERIES
    // ========================================

    /**
     * Get konami progress for a player.
     */
    uint8_t getKonamiProgress(int playerIndex) {
        DeviceInstance& player = getPlayer(playerIndex);
        return player.player->getKonamiProgress();
    }

    /**
     * Check if player has unlocked specific button.
     */
    bool hasUnlockedButton(int playerIndex, KonamiButton button) {
        DeviceInstance& player = getPlayer(playerIndex);
        return player.player->hasUnlockedButton(static_cast<uint8_t>(button));
    }

    /**
     * Check if player has completed konami code (all 7 buttons).
     */
    bool isKonamiComplete(int playerIndex) {
        DeviceInstance& player = getPlayer(playerIndex);
        return player.player->isKonamiComplete();
    }

    /**
     * Check if player has color profile eligibility for game type.
     */
    bool hasColorProfileEligibility(int playerIndex, GameType gameType) {
        DeviceInstance& player = getPlayer(playerIndex);
        return player.player->hasColorProfileEligibility(static_cast<int>(gameType));
    }

    // ========================================
    // FDN WORKFLOW HELPERS
    // ========================================

    /**
     * Trigger FDN handshake between player and NPC.
     * This simulates the cable connection and serial handshake.
     *
     * @param playerIndex Player device index
     * @param npcIndex NPC device index
     * @param ticks Number of ticks to process handshake (default 10)
     */
    void triggerFdnHandshake(int playerIndex, int npcIndex, int ticks = 10) {
        connectCable(playerIndex, players_.size() + npcIndex);
        tick(ticks);
    }

    /**
     * Wait for player to return to Idle after game completion.
     * Times out after maxTicks to prevent infinite loops.
     *
     * @param playerIndex Player device index
     * @param maxTicks Maximum ticks to wait (default 100)
     * @return true if player reached Idle, false if timeout
     */
    bool waitForIdle(int playerIndex, int maxTicks = 100) {
        for (int i = 0; i < maxTicks; i++) {
            if (isPlayerInState(playerIndex, IDLE)) {
                return true;
            }
            tickWithTime(1, 100);
        }
        return false;  // Timeout
    }

    /**
     * Execute full FDN encounter: connect, play, wait for completion.
     * This is a simplified helper for happy-path testing.
     *
     * @param playerIndex Player device index
     * @param npcIndex NPC device index
     * @return true if encounter completed successfully (player returned to Idle)
     */
    bool executeFullEncounter(int playerIndex, int npcIndex) {
        // Connect and trigger handshake
        triggerFdnHandshake(playerIndex, npcIndex, 10);

        // Wait for game to start (FdnDetected → minigame app)
        tickWithTime(20, 100);

        // Allow time for game to complete (simplified - real tests need game-specific logic)
        tickWithTime(100, 100);

        // Wait for return to Idle
        return waitForIdle(playerIndex, 50);
    }

private:
    struct PlayerConfig {
        int deviceIndex;
        bool isHunter;
    };

    struct NpcConfig {
        int deviceIndex;
        GameType gameType;
    };

    std::vector<PlayerConfig> players_;
    std::vector<NpcConfig> npcs_;
    std::vector<DeviceInstance> devices_;
};

#endif // NATIVE_BUILD
