#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <deque>
#include <vector>
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
#include "device/remote-device-coordinator.hpp"
#include "game/player.hpp"
#include "game/game-session.hpp"
#include "game/quickdraw-apps.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "apps/player-fetch/player-fetch.hpp"
#include "game/player-provider.hpp"

// CLI components
#include "apps/player-fetch/player-fetch-states.hpp"
#include "game/quickdraw-states.hpp"

#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"

namespace cli {

/**
 * Human-readable name for a state id shown in the CLI's state history.
 *
 * Ids come from two namespaces that share one numbering: player-fetch owns
 * 0-4 (and 31) and the game owns the rest. Naming the constants rather than the
 * numbers is what keeps this from drifting — a retired state stops compiling here.
 */
inline const char* getStateName(int stateId) {
    switch (stateId) {
        case PlayerFetchStateId::PIN_ENTRY: return "PinEntry";
        case PlayerFetchStateId::FETCH_USER: return "FetchUser";
        case PlayerFetchStateId::CHOOSE_TEAM: return "ChooseTeam";
        case PlayerFetchStateId::WELCOME: return "WelcomeMessage";
        case PlayerFetchStateId::FIRMWARE_UPDATE: return "FirmwareUpdate";
        case SLEEP: return "Sleep";
        case AWAKEN_SEQUENCE: return "AwakenSequence";
        case IDLE: return "Idle";
        case MATCH_COUNTDOWN: return "MatchCountdown";
        case MATCH: return "MatchState";
        case MATCH_PUSHED: return "MatchPushed";
        case MATCH_RECEIVED_RESULT: return "MatchReceivedResult";
        case MATCH_RESULT: return "MatchResult";
        case WIN: return "Win";
        case LOSE: return "Lose";
        case SUPPORTER_READY: return "SupporterReady";
        case SHOOTOUT_PROPOSAL: return "ShootoutProposal";
        case SHOOTOUT_BRACKET_REVEAL: return "ShootoutBracketReveal";
        case SHOOTOUT_SPECTATOR: return "ShootoutSpectator";
        case SHOOTOUT_ELIMINATED: return "ShootoutEliminated";
        case SHOOTOUT_FINAL_STANDINGS: return "ShootoutFinalStandings";
        case SHOOTOUT_ABORTED: return "ShootoutAborted";
        case SYMBOL: return "Symbol";
        case SYMBOL_MATCHED: return "SymbolMatched";
        default: return "Unknown";
    }
}

/**
 * Structure to hold all components for a single simulated PDN device.
 */
struct DeviceInstance {
    int deviceIndex;
    std::string deviceId;  // e.g., "0010", "0011", etc.
    bool isHunter;
    
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
    PlayerProvider* playerProvider = nullptr;
    Player* player = nullptr;
    GameSession* gameSession = nullptr;
    PlayerFetchApp* playerFetchApp = nullptr;
    AppConfig apps;
    SymbolWirelessManager* symbolWirelessManager = nullptr;

    // State history (circular buffer, most recent at back)
    std::deque<int> stateHistory;

    /**
     * State the device is actually running, from whichever app is mounted.
     */
    State* getCurrentState() const {
        StateMachine* app = pdn ? pdn->getActiveApp() : nullptr;
        return app ? app->getCurrentState() : nullptr;
    }

    /**
     * Track state transitions for display in the UI.
     */
    void updateStateHistory(int currentStateId) {
        if (currentStateId != (stateHistory.empty() ? -1 : stateHistory.back())) {
            stateHistory.push_back(currentStateId);
            while (stateHistory.size() > 4) {
                stateHistory.pop_front();
            }
        }
    }

    /**
     * The `reboot` command: forget the UI's state history and remount
     * provisioning at its fetch state, with the device id as the PIN.
     */
    void reboot() {
        stateHistory.clear();
        playerProvider->setRegistrationPin(deviceId.c_str());
        pdn->setActiveApp(StateId(PLAYER_FETCH_APP_ID), StateId(PlayerFetchStateId::FETCH_USER));
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
        instance.loggerDriver->setSuppressOutput(getenv("PDN_CLI_LOG_FILE") == nullptr);
        instance.clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME + suffix);
        instance.displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        instance.primaryButtonDriver = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
        instance.secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        instance.lightDriver = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        instance.hapticsDriver = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix);
        instance.serialOutDriver = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        instance.serialInDriver = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        instance.httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        instance.storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix, {MATCHES_PREFS_NAMESPACE});
        
        // Configure mock HTTP server with this device's player data
        MockPlayerConfig playerConfig;
        playerConfig.name = (isHunter ? "Hunter" : "Bounty") + instance.deviceId;
        UUIDBytes::parse("7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11", playerConfig.wirePlayerId);
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

        instance.playerProvider = new PlayerProvider();
        instance.player = instance.playerProvider->getPlayer();
        char* idPtr = new char[5];
        strcpy(idPtr, instance.deviceId.c_str());
        instance.player->setUserID(idPtr);
        instance.player->setIsHunter(isHunter);
        instance.player->setAllegiance(Allegiance::RESISTANCE);  // Default allegiance
        instance.playerProvider->setRegistrationPin(instance.deviceId.c_str());

        instance.symbolWirelessManager = new SymbolWirelessManager(
            instance.pdn->getWirelessManager(),
            instance.pdn->getRemoteDeviceCoordinator());

        // Create the shared managers and the apps that read them
        instance.gameSession =
        new GameSession(instance.player, instance.playerProvider->getRecordKey(), instance.pdn,
                        instance.symbolWirelessManager);

        instance.playerFetchApp = new PlayerFetchApp(
            instance.pdn->getWirelessManager(),
            instance.pdn->getFirmwareUpdateManager(),
            instance.playerProvider);
        instance.apps = makeGameplayApps(instance.gameSession->getContext());
        instance.apps[StateId(PLAYER_FETCH_APP_ID)] = instance.playerFetchApp;
        // Skip the entire registration flow: launch straight into the hub's first
        // state (AwakenSequence). The player is already configured with ID, role,
        // and allegiance from DeviceFactory.
        instance.pdn->loadAppConfig(instance.apps, StateId(HUB_APP_ID));
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

        // Apps before the session, which owns the managers their GameContext
        // copies point at: ~SupporterReady dereferences one when destroyed mounted.
        for (const std::pair<const StateId, StateMachine*>& entry : device.apps) {
            delete entry.second;
        }
        delete device.gameSession;
        delete device.symbolWirelessManager;
        delete device.playerProvider;
        delete device.pdn;
        // Note: drivers are owned by DriverManager via PDN, so they're deleted when PDN is deleted
    }
};

/**
 * One simulator tick for every device: delivers queued ESP-NOW packets, emits
 * one HELLO on each device's jacks, runs each device's loop, then records the
 * state it ended in.
 */
inline void advanceSimulation(std::vector<DeviceInstance>& devices) {
    NativePeerBroker::getInstance().deliverPackets();

    // On ESP32 a FreeRTOS task emits HELLO every HELLO_CADENCE_MS; native builds
    // spawn no task, so without this cabled devices never link. One HELLO goes
    // out per tick, so a tick longer than HELLO_SILENT_LINK_MS (a render or
    // terminal stall) drops the links until the next exchange.
    for (DeviceInstance& device : devices) {
        device.pdn->getRemoteDeviceCoordinator()->emitHello();
    }

    for (DeviceInstance& device : devices) {
        device.pdn->loop();
        State* currentState = device.getCurrentState();
        device.updateStateHistory(currentState ? currentState->getStateId() : -1);
    }
}

} // namespace cli

#endif // NATIVE_BUILD
