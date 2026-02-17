#ifdef NATIVE_BUILD

#include "cli/cli-device.hpp"

// Full driver includes — needed by DeviceFactory implementations
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
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/peer-comms-types.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include <cstring>

namespace cli {

DeviceInstance DeviceFactory::createDevice(int deviceIndex, bool isHunter) {
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

    // Set platform clock BEFORE loadAppConfig — game states use it during onStateMounted()
    SimpleTimer::setPlatformClock(instance.clockDriver);

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

void DeviceFactory::destroyDevice(DeviceInstance& device) {
    // Unregister from SerialCableBroker
    SerialCableBroker::getInstance().unregisterDevice(device.deviceIndex);

    // Remove player config from mock HTTP server
    MockHttpServer::getInstance().removePlayer(device.deviceId);

    // Clear platform clock BEFORE device destruction to prevent pure virtual call
    // State dismount callbacks may call SimpleTimer::updateTime() which needs valid clock or nullptr
    SimpleTimer::setPlatformClock(nullptr);

    // Delete PDN first — its destructor dismounts apps, which may call
    // quickdrawWirelessManager->clearCallbacks(). Must happen before
    // deleting the wireless manager.
    delete device.pdn;
    // Note: device.game and minigames are deleted by PDN destructor
    delete device.quickdrawWirelessManager;
    delete device.player;
    // Note: drivers are owned by DriverManager via PDN, so they're deleted when PDN is deleted
}

DeviceInstance DeviceFactory::createFdnDevice(int deviceIndex, GameType gameType) {
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

    // Set platform clock BEFORE loadAppConfig — game states use it during onStateMounted()
    SimpleTimer::setPlatformClock(instance.clockDriver);

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

DeviceInstance DeviceFactory::createGameDevice(int deviceIndex, const std::string& gameName) {
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

    // Set platform clock BEFORE loadAppConfig — game states use it during onStateMounted()
    SimpleTimer::setPlatformClock(instance.clockDriver);

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

} // namespace cli

#endif // NATIVE_BUILD
