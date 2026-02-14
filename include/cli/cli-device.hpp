#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <deque>
#include <cstring>

// Native drivers
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-display-driver.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/drivers/native/native-haptics-driver.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"

// Core game components
#include "device/pdn.hpp"
#include "game/player.hpp"
#include "game/quickdraw.hpp"
#include "game/fdn-game.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/exploit-sequencer/exploit-sequencer-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-states.hpp"
#include "game/konami-metagame/konami-metagame.hpp"
#include "device/device-types.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/peer-comms-types.hpp"

// CLI components
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "cli/cli-duel.hpp"

namespace cli {

inline const char* getQuickdrawStateName(int stateId) {
    switch (stateId) {
        case 0:  return "PlayerRegistration";
        case 1:  return "FetchUserData";
        case 2:  return "ConfirmOffline";
        case 3:  return "ChooseRole";
        case 4:  return "AllegiancePicker";
        case 5:  return "WelcomeMessage";
        case 6:  return "Sleep";
        case 7:  return "AwakenSequence";
        case 8:  return "Idle";
        case 9:  return "HandshakeInitiate";
        case 10: return "BountySendCC";
        case 11: return "HunterSendId";
        case 12: return "ConnectionSuccessful";
        case 13: return "DuelCountdown";
        case 14: return "Duel";
        case 15: return "DuelPushed";
        case 16: return "DuelReceivedResult";
        case 17: return "DuelResult";
        case 18: return "Win";
        case 19: return "Lose";
        case 20: return "UploadMatches";
        case 21: return "FdnDetected";
        case 22: return "FdnComplete";
        case 23: return "ColorProfilePrompt";
        case 24: return "ColorProfilePicker";
        case 25: return "FdnReencounter";
        case 26: return "KonamiPuzzle";
        default: return "Unknown";
    }
}

inline const char* getSignalEchoStateName(int stateId) {
    switch (stateId) {
        case ECHO_INTRO:         return "EchoIntro";
        case ECHO_SHOW_SEQUENCE: return "EchoShowSequence";
        case ECHO_PLAYER_INPUT:  return "EchoPlayerInput";
        case ECHO_EVALUATE:      return "EchoEvaluate";
        case ECHO_WIN:           return "EchoWin";
        case ECHO_LOSE:          return "EchoLose";
        default:                 return "Unknown";
    }
}

inline const char* getFirewallDecryptStateName(int stateId) {
    switch (stateId) {
        case DECRYPT_INTRO:    return "DecryptIntro";
        case DECRYPT_SCAN:     return "DecryptScan";
        case DECRYPT_EVALUATE: return "DecryptEvaluate";
        case DECRYPT_WIN:      return "DecryptWin";
        case DECRYPT_LOSE:     return "DecryptLose";
        default:               return "Unknown";
    }
}

inline const char* getGhostRunnerStateName(int stateId) {
    switch (stateId) {
        case GHOST_INTRO:    return "GhostRunnerIntro";
        case GHOST_WIN:      return "GhostRunnerWin";
        case GHOST_LOSE:     return "GhostRunnerLose";
        case GHOST_SHOW:     return "GhostRunnerShow";
        case GHOST_GAMEPLAY: return "GhostRunnerGameplay";
        case GHOST_EVALUATE: return "GhostRunnerEvaluate";
        default:             return "Unknown";
    }
}

inline const char* getSpikeVectorStateName(int stateId) {
    switch (stateId) {
        case SPIKE_INTRO:    return "SpikeVectorIntro";
        case SPIKE_WIN:      return "SpikeVectorWin";
        case SPIKE_LOSE:     return "SpikeVectorLose";
        case SPIKE_SHOW:     return "SpikeVectorShow";
        case SPIKE_GAMEPLAY: return "SpikeVectorGameplay";
        case SPIKE_EVALUATE: return "SpikeVectorEvaluate";
        default:             return "Unknown";
    }
}

inline const char* getCipherPathStateName(int stateId) {
    switch (stateId) {
        case CIPHER_INTRO:    return "CipherPathIntro";
        case CIPHER_WIN:      return "CipherPathWin";
        case CIPHER_LOSE:     return "CipherPathLose";
        case CIPHER_SHOW:     return "CipherPathShow";
        case CIPHER_GAMEPLAY: return "CipherPathGameplay";
        case CIPHER_EVALUATE: return "CipherPathEvaluate";
        default:              return "Unknown";
    }
}

inline const char* getExploitSequencerStateName(int stateId) {
    switch (stateId) {
        case EXPLOIT_INTRO:    return "ExploitSeqIntro";
        case EXPLOIT_WIN:      return "ExploitSeqWin";
        case EXPLOIT_LOSE:     return "ExploitSeqLose";
        case EXPLOIT_SHOW:     return "ExploitSeqShow";
        case EXPLOIT_GAMEPLAY: return "ExploitSeqGameplay";
        case EXPLOIT_EVALUATE: return "ExploitSeqEvaluate";
        default:               return "Unknown";
    }
}

inline const char* getBreachDefenseStateName(int stateId) {
    switch (stateId) {
        case BREACH_INTRO:    return "BreachDefenseIntro";
        case BREACH_WIN:      return "BreachDefenseWin";
        case BREACH_LOSE:     return "BreachDefenseLose";
        case BREACH_SHOW:     return "BreachDefenseShow";
        case BREACH_GAMEPLAY: return "BreachDefenseGameplay";
        case BREACH_EVALUATE: return "BreachDefenseEvaluate";
        default:              return "Unknown";
    }
}

inline const char* getFdnStateName(int stateId) {
    switch (stateId) {
        case 0: return "NpcIdle";
        case 1: return "NpcHandshake";
        case 2: return "NpcGameActive";
        case 3: return "NpcReceiveResult";
        default: return "Unknown";
    }
}

inline const char* getStateName(int stateId, DeviceType deviceType = DeviceType::PLAYER, GameType gameType = GameType::QUICKDRAW) {
    if (deviceType == DeviceType::FDN) {
        return getFdnStateName(stateId);
    }
    // Breach Defense state IDs are in the 700+ range
    if (stateId >= BREACH_INTRO) {
        return getBreachDefenseStateName(stateId);
    }
    // Exploit Sequencer state IDs are in the 600+ range
    if (stateId >= EXPLOIT_INTRO) {
        return getExploitSequencerStateName(stateId);
    }
    // Cipher Path state IDs are in the 500+ range
    if (stateId >= CIPHER_INTRO) {
        return getCipherPathStateName(stateId);
    }
    // Spike Vector state IDs are in the 400+ range
    if (stateId >= SPIKE_INTRO) {
        return getSpikeVectorStateName(stateId);
    }
    // Ghost Runner state IDs are in the 300+ range
    if (stateId >= GHOST_INTRO) {
        return getGhostRunnerStateName(stateId);
    }
    // Firewall Decrypt state IDs are in the 200+ range
    if (stateId >= DECRYPT_INTRO) {
        return getFirewallDecryptStateName(stateId);
    }
    // Signal Echo state IDs are in the 100+ range
    if (stateId >= ECHO_INTRO) {
        return getSignalEchoStateName(stateId);
    }
    return getQuickdrawStateName(stateId);
}

/**
 * Structure to hold all components for a single simulated PDN device.
 */
struct DeviceInstance {
    int deviceIndex;
    std::string deviceId;  // e.g., "0010", "0011", etc.
    bool isHunter;
    DeviceType deviceType = DeviceType::PLAYER;
    GameType gameType = GameType::QUICKDRAW;

    // Native drivers
    NativeClockDriver* clockDriver = nullptr;
    NativeLoggerDriver* loggerDriver = nullptr;
    NativeDisplayDriver* displayDriver = nullptr;
    NativeButtonDriver* primaryButtonDriver = nullptr;
    NativeButtonDriver* secondaryButtonDriver = nullptr;
    NativeLightStripDriver* lightDriver = nullptr;
    NativeHapticsDriver* hapticsDriver = nullptr;
    NativeSerialDriver* serialOutDriver = nullptr;
    NativeSerialDriver* serialInDriver = nullptr;
    NativeHttpClientDriver* httpClientDriver = nullptr;
    NativePeerCommsDriver* peerCommsDriver = nullptr;
    NativePrefsDriver* storageDriver = nullptr;
    
    // Game objects
    PDN* pdn = nullptr;
    Player* player = nullptr;
    StateMachine* game = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
    
    // State history (circular buffer, most recent at back)
    std::deque<int> stateHistory;
    int lastStateId = -1;

    // Duel tracking
    DuelHistory duelHistory;
    SeriesState seriesState;
    bool rematchPending = false;
    bool duelRecordedThisSession = false;  // Prevents duplicate recording

    /**
     * Track state transitions for display in the UI.
     */
    void updateStateHistory(int currentStateId) {
        if (currentStateId != lastStateId) {
            stateHistory.push_back(currentStateId);
            while (stateHistory.size() > 4) {
                stateHistory.pop_front();
            }
            lastStateId = currentStateId;
        }
    }
};

/**
 * Factory class for creating and managing DeviceInstance objects.
 */
class DeviceFactory {
public:
    /**
     * Create a new simulated PDN device.
     * 
     * @param deviceIndex Index for this device (used to generate ID and driver names)
     * @param isHunter Whether this device should be configured as a hunter (vs bounty)
     * @return Fully initialized DeviceInstance
     */
    static DeviceInstance createDevice(int deviceIndex, bool isHunter) {
        DeviceInstance instance;
        instance.deviceIndex = deviceIndex;
        instance.isHunter = isHunter;
        
        // Generate device ID: 0010, 0011, 0012, etc.
        char idBuffer[5];
        snprintf(idBuffer, sizeof(idBuffer), "%04d", 10 + deviceIndex);
        instance.deviceId = idBuffer;
        
        // Create all drivers with device-specific suffix
        std::string suffix = "_" + std::to_string(deviceIndex);
        
        instance.loggerDriver = new NativeLoggerDriver(LOGGER_DRIVER_NAME + suffix);
        instance.loggerDriver->setSuppressOutput(true);  // Suppress direct output - displayed in UI
        instance.clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME + suffix);
        instance.displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        instance.primaryButtonDriver = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
        instance.secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        instance.lightDriver = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        instance.hapticsDriver = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
        instance.serialOutDriver = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        instance.serialInDriver = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        instance.httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        instance.httpClientDriver->setMockServerEnabled(true);  // Enable mock HTTP server
        instance.httpClientDriver->setConnected(true);  // Simulate WiFi connection
        instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        instance.storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);
        
        // Configure mock HTTP server with this device's player data
        MockPlayerConfig playerConfig;
        playerConfig.id = instance.deviceId;
        playerConfig.name = (isHunter ? "Hunter" : "Bounty") + instance.deviceId;
        playerConfig.isHunter = isHunter;
        playerConfig.allegiance = static_cast<int>(Allegiance::RESISTANCE);  // RESISTANCE
        playerConfig.faction = isHunter ? "Guild" : "Rebels";
        MockHttpServer::getInstance().configurePlayer(instance.deviceId, playerConfig);
        
        // Create driver configuration
        DriverConfig pdnConfig = {
            {DISPLAY_DRIVER_NAME, instance.displayDriver},
            {PRIMARY_BUTTON_DRIVER_NAME, instance.primaryButtonDriver},
            {SECONDARY_BUTTON_DRIVER_NAME, instance.secondaryButtonDriver},
            {LIGHT_DRIVER_NAME, instance.lightDriver},
            {HAPTICS_DRIVER_NAME, instance.hapticsDriver},
            {SERIAL_OUT_DRIVER_NAME, instance.serialOutDriver},
            {SERIAL_IN_DRIVER_NAME, instance.serialInDriver},
            {HTTP_CLIENT_DRIVER_NAME, instance.httpClientDriver},
            {PEER_COMMS_DRIVER_NAME, instance.peerCommsDriver},
            {PLATFORM_CLOCK_DRIVER_NAME, instance.clockDriver},
            {LOGGER_DRIVER_NAME, instance.loggerDriver},
            {STORAGE_DRIVER_NAME, instance.storageDriver},
        };
        
        // Create PDN
        instance.pdn = PDN::createPDN(pdnConfig);
        instance.pdn->begin();
        
        // Create and configure player
        instance.player = new Player();
        char* idPtr = new char[5];
        strcpy(idPtr, instance.deviceId.c_str());
        instance.player->setUserID(idPtr);
        instance.player->setIsHunter(isHunter);
        instance.player->setAllegiance(Allegiance::RESISTANCE);  // Default allegiance
        
        // Create QuickdrawWirelessManager (required by game states even when mocking)
        instance.quickdrawWirelessManager = new QuickdrawWirelessManager();
        instance.quickdrawWirelessManager->initialize(instance.player, instance.pdn->getWirelessManager(), 1000);
        
        // Register ESP-NOW packet handlers (similar to setupEspNow in main.cpp)
        // This is required for devices to actually receive and process ESP-NOW packets
        instance.pdn->getWirelessManager()->setEspNowPacketHandler(
            PktType::kQuickdrawCommand,
            [](const uint8_t* src, const uint8_t* data, const size_t len, void* userArg) {
                ((QuickdrawWirelessManager*)userArg)->processQuickdrawCommand(src, data, len);
            },
            instance.quickdrawWirelessManager
        );
        
        // Create game (no remote debug manager for now)
        instance.game = new Quickdraw(instance.player, instance.pdn, instance.quickdrawWirelessManager, nullptr);

        // Create minigames (pre-registered, configured lazily per encounter)
        auto* signalEcho = new SignalEcho(SIGNAL_ECHO_EASY);
        auto* firewallDecrypt = new FirewallDecrypt(FIREWALL_DECRYPT_EASY);
        auto* ghostRunner = new GhostRunner(GHOST_RUNNER_EASY);
        auto* spikeVector = new SpikeVector(SPIKE_VECTOR_EASY);
        auto* cipherPath = new CipherPath(CIPHER_PATH_EASY);
        auto* exploitSequencer = new ExploitSequencer(EXPLOIT_SEQUENCER_EASY);
        auto* breachDefense = new BreachDefense(BREACH_DEFENSE_EASY);

        // Create Konami metagame (manages FDN progression and rewards)
        auto* konamiMetaGame = new KonamiMetaGame(instance.player);

        // Register state machines with the device and launch Quickdraw
        AppConfig apps = {
            {StateId(QUICKDRAW_APP_ID), instance.game},
            {StateId(SIGNAL_ECHO_APP_ID), signalEcho},
            {StateId(FIREWALL_DECRYPT_APP_ID), firewallDecrypt},
            {StateId(GHOST_RUNNER_APP_ID), ghostRunner},
            {StateId(SPIKE_VECTOR_APP_ID), spikeVector},
            {StateId(CIPHER_PATH_APP_ID), cipherPath},
            {StateId(EXPLOIT_SEQUENCER_APP_ID), exploitSequencer},
            {StateId(BREACH_DEFENSE_APP_ID), breachDefense},
            {StateId(KONAMI_METAGAME_APP_ID), konamiMetaGame}
        };
        instance.pdn->loadAppConfig(apps, StateId(QUICKDRAW_APP_ID));
        
        // Skip PlayerRegistration state and go directly to FetchUserDataState
        // This prevents the registration flow from overwriting the player ID
        // State index 1 = FetchUserDataState (based on Quickdraw::populateStateMap order)
        instance.game->skipToState(instance.pdn, 1);
        
        // Register with SerialCableBroker for cable simulation
        SerialCableBroker::getInstance().registerDevice(
            deviceIndex, instance.serialOutDriver, instance.serialInDriver, isHunter);
        
        return instance;
    }
    
    /**
     * Clean up a device instance and free all resources.
     */
    static void destroyDevice(DeviceInstance& device) {
        // Unregister from SerialCableBroker
        SerialCableBroker::getInstance().unregisterDevice(device.deviceIndex);

        // Remove player config from mock HTTP server
        MockHttpServer::getInstance().removePlayer(device.deviceId);

        // Note: device.game and minigames are now deleted by PDN destructor (Device::~Device)
        delete device.quickdrawWirelessManager;
        delete device.player;
        delete device.pdn;
        // Note: drivers are owned by DriverManager via PDN, so they're deleted when PDN is deleted
    }

    /**
     * Create a new simulated FDN (Fixed Data Node) NPC device.
     *
     * @param deviceIndex Index for this device
     * @param gameType Which game this FDN hosts
     * @return Fully initialized DeviceInstance running FdnGame
     */
    static DeviceInstance createFdnDevice(int deviceIndex, GameType gameType) {
        DeviceInstance instance;
        instance.deviceIndex = deviceIndex;
        instance.isHunter = true;  // FDN uses output jack as primary (same as hunter)
        instance.deviceType = DeviceType::FDN;
        instance.gameType = gameType;

        // Generate device ID: 7010, 7011, etc. (7xxx range for FDNs)
        char idBuffer[5];
        snprintf(idBuffer, sizeof(idBuffer), "7%03d", 10 + deviceIndex);
        instance.deviceId = idBuffer;

        // Create all drivers with device-specific suffix
        std::string suffix = "_" + std::to_string(deviceIndex);

        instance.loggerDriver = new NativeLoggerDriver(LOGGER_DRIVER_NAME + suffix);
        instance.loggerDriver->setSuppressOutput(true);
        instance.clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME + suffix);
        instance.displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        instance.primaryButtonDriver = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
        instance.secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        instance.lightDriver = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        instance.hapticsDriver = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
        instance.serialOutDriver = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        instance.serialInDriver = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        instance.httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        instance.httpClientDriver->setMockServerEnabled(true);
        instance.httpClientDriver->setConnected(true);
        instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        instance.storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);

        DriverConfig pdnConfig = {
            {DISPLAY_DRIVER_NAME, instance.displayDriver},
            {PRIMARY_BUTTON_DRIVER_NAME, instance.primaryButtonDriver},
            {SECONDARY_BUTTON_DRIVER_NAME, instance.secondaryButtonDriver},
            {LIGHT_DRIVER_NAME, instance.lightDriver},
            {HAPTICS_DRIVER_NAME, instance.hapticsDriver},
            {SERIAL_OUT_DRIVER_NAME, instance.serialOutDriver},
            {SERIAL_IN_DRIVER_NAME, instance.serialInDriver},
            {HTTP_CLIENT_DRIVER_NAME, instance.httpClientDriver},
            {PEER_COMMS_DRIVER_NAME, instance.peerCommsDriver},
            {PLATFORM_CLOCK_DRIVER_NAME, instance.clockDriver},
            {LOGGER_DRIVER_NAME, instance.loggerDriver},
            {STORAGE_DRIVER_NAME, instance.storageDriver},
        };

        instance.pdn = PDN::createPDN(pdnConfig);
        instance.pdn->begin();
        instance.pdn->setActiveComms(SerialIdentifier::OUTPUT_JACK);

        instance.player = nullptr;
        instance.quickdrawWirelessManager = nullptr;

        KonamiButton reward = getRewardForGame(gameType);
        instance.game = new FdnGame(gameType, reward);

        AppConfig apps = {
            {StateId(FDN_GAME_APP_ID), instance.game}
        };
        instance.pdn->loadAppConfig(apps, StateId(FDN_GAME_APP_ID));

        SerialCableBroker::getInstance().registerDevice(
            deviceIndex, instance.serialOutDriver, instance.serialInDriver, true);

        return instance;
    }

    /**
     * Create a standalone game device (e.g., Signal Echo in standalone mode).
     *
     * @param deviceIndex Index for this device
     * @param gameName Name of the game to launch ("signal-echo")
     * @return Fully initialized DeviceInstance running the specified game
     */
    static DeviceInstance createGameDevice(int deviceIndex, const std::string& gameName) {
        DeviceInstance instance;
        instance.deviceIndex = deviceIndex;
        instance.isHunter = false;
        instance.deviceType = DeviceType::PLAYER;

        char idBuffer[5];
        snprintf(idBuffer, sizeof(idBuffer), "%04d", 10 + deviceIndex);
        instance.deviceId = idBuffer;

        std::string suffix = "_" + std::to_string(deviceIndex);

        instance.loggerDriver = new NativeLoggerDriver(LOGGER_DRIVER_NAME + suffix);
        instance.loggerDriver->setSuppressOutput(true);
        instance.clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME + suffix);
        instance.displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        instance.primaryButtonDriver = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
        instance.secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        instance.lightDriver = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        instance.hapticsDriver = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
        instance.serialOutDriver = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        instance.serialInDriver = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        instance.httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        instance.httpClientDriver->setMockServerEnabled(true);
        instance.httpClientDriver->setConnected(true);
        instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        instance.storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);

        DriverConfig pdnConfig = {
            {DISPLAY_DRIVER_NAME, instance.displayDriver},
            {PRIMARY_BUTTON_DRIVER_NAME, instance.primaryButtonDriver},
            {SECONDARY_BUTTON_DRIVER_NAME, instance.secondaryButtonDriver},
            {LIGHT_DRIVER_NAME, instance.lightDriver},
            {HAPTICS_DRIVER_NAME, instance.hapticsDriver},
            {SERIAL_OUT_DRIVER_NAME, instance.serialOutDriver},
            {SERIAL_IN_DRIVER_NAME, instance.serialInDriver},
            {HTTP_CLIENT_DRIVER_NAME, instance.httpClientDriver},
            {PEER_COMMS_DRIVER_NAME, instance.peerCommsDriver},
            {PLATFORM_CLOCK_DRIVER_NAME, instance.clockDriver},
            {LOGGER_DRIVER_NAME, instance.loggerDriver},
            {STORAGE_DRIVER_NAME, instance.storageDriver},
        };

        instance.pdn = PDN::createPDN(pdnConfig);
        instance.pdn->begin();

        instance.player = nullptr;
        instance.quickdrawWirelessManager = nullptr;

        if (gameName == "signal-echo") {
            instance.gameType = GameType::SIGNAL_ECHO;
            instance.game = new SignalEcho(SIGNAL_ECHO_EASY);

            AppConfig apps = {
                {StateId(SIGNAL_ECHO_APP_ID), instance.game}
            };
            instance.pdn->loadAppConfig(apps, StateId(SIGNAL_ECHO_APP_ID));
        } else if (gameName == "firewall-decrypt") {
            instance.gameType = GameType::FIREWALL_DECRYPT;
            instance.game = new FirewallDecrypt(FIREWALL_DECRYPT_EASY);

            AppConfig apps = {
                {StateId(FIREWALL_DECRYPT_APP_ID), instance.game}
            };
            instance.pdn->loadAppConfig(apps, StateId(FIREWALL_DECRYPT_APP_ID));
        } else if (gameName == "ghost-runner") {
            instance.gameType = GameType::GHOST_RUNNER;
            instance.game = new GhostRunner(GHOST_RUNNER_EASY);

            AppConfig apps = {
                {StateId(GHOST_RUNNER_APP_ID), instance.game}
            };
            instance.pdn->loadAppConfig(apps, StateId(GHOST_RUNNER_APP_ID));
        } else if (gameName == "spike-vector") {
            instance.gameType = GameType::SPIKE_VECTOR;
            instance.game = new SpikeVector(SPIKE_VECTOR_EASY);

            AppConfig apps = {
                {StateId(SPIKE_VECTOR_APP_ID), instance.game}
            };
            instance.pdn->loadAppConfig(apps, StateId(SPIKE_VECTOR_APP_ID));
        } else if (gameName == "cipher-path") {
            instance.gameType = GameType::CIPHER_PATH;
            instance.game = new CipherPath(CIPHER_PATH_EASY);

            AppConfig apps = {
                {StateId(CIPHER_PATH_APP_ID), instance.game}
            };
            instance.pdn->loadAppConfig(apps, StateId(CIPHER_PATH_APP_ID));
        } else if (gameName == "exploit-sequencer") {
            instance.gameType = GameType::EXPLOIT_SEQUENCER;
            instance.game = new ExploitSequencer(EXPLOIT_SEQUENCER_EASY);

            AppConfig apps = {
                {StateId(EXPLOIT_SEQUENCER_APP_ID), instance.game}
            };
            instance.pdn->loadAppConfig(apps, StateId(EXPLOIT_SEQUENCER_APP_ID));
        } else if (gameName == "breach-defense") {
            instance.gameType = GameType::BREACH_DEFENSE;
            instance.game = new BreachDefense(BREACH_DEFENSE_EASY);

            AppConfig apps = {
                {StateId(BREACH_DEFENSE_APP_ID), instance.game}
            };
            instance.pdn->loadAppConfig(apps, StateId(BREACH_DEFENSE_APP_ID));
        }

        return instance;
    }
};

} // namespace cli

#endif // NATIVE_BUILD
