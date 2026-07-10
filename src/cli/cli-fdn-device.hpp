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

// FDN-specific headers (resolved via -I src/fdn)
#include "device/fdn.hpp"
#include "fdn-constants.hpp"
#include "game/player.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "wireless/remote-player-manager.hpp"
#include "apps/symbol-lock/symbol-lock.hpp"
#include "apps/demo-modules/demo-module.hpp"
#include "apps/fdn-app-ids.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/driver-names.hpp"

// CLI components
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"

namespace cli {

inline const char* getFDNStateName(int stateId) {
    switch (stateId) {
        case 0:  return "MainMenu";
        case 1:  return "Tutorial";
        case 2:  return "Game";
        case 3:  return "Scoring";
        default: return "Unknown";
    }
}

/**
 * Structure holding all components for a single simulated FDN device.
 */
struct FDNInstance {
    int fdnIndex = 0;
    std::string fdnId;

    // Native drivers
    NativeClockDriver*        clockDriver                = nullptr;
    NativeLoggerDriver*       loggerDriver               = nullptr;
    NativeDisplayDriver*      displayDriver              = nullptr;
    NativeButtonDriver*       primaryButtonDriver        = nullptr;
    NativeButtonDriver*       secondaryButtonDriver      = nullptr;
    NativeButtonDriver*       tertiaryButtonDriver       = nullptr;
    NativeLightStripDriver*   lightDriver                = nullptr;
    NativeHapticsDriver*      hapticsDriver              = nullptr;
    NativeSerialDriver*       serialInDriver             = nullptr;
    NativeSerialDriver*       serialInSecondaryDriver    = nullptr;
    NativeHttpClientDriver*   httpClientDriver           = nullptr;
    NativePeerCommsDriver*    peerCommsDriver            = nullptr;
    NativePrefsDriver*        storageDriver              = nullptr;

    // Device
    FDN* fdn = nullptr;

    // Managers
    RemotePlayerManager*       remotePlayerManager       = nullptr;
    FDNConnectWirelessManager* fdnConnectWirelessManager = nullptr;
    SymbolWirelessManager*     symbolWirelessManager     = nullptr;
    ControllerWirelessManager* controllerWirelessManager = nullptr;

    // Apps
    SymbolLock* symbolLockApp = nullptr;
    DemoModule* demoModule    = nullptr;

    // State tracking
    std::deque<int> stateHistory;
    int lastStateId = -1;

    void updateStateHistory(int currentStateId) {
        if (currentStateId != lastStateId) {
            stateHistory.push_back(currentStateId);
            while (stateHistory.size() > 4) stateHistory.pop_front();
            lastStateId = currentStateId;
        }
    }
};

/**
 * Factory for creating and managing FDNInstance objects.
 */
class FDNFactory {
public:
    /**
     * Create a simulated FDN device.
     * Launches directly into DemoModule (skips SymbolLock ceremony).
     * @param fdnIndex Unique index for this FDN
     */
    static FDNInstance createFDN(int fdnIndex) {
        FDNInstance instance;
        instance.fdnIndex = fdnIndex;

        char idBuf[8];
        snprintf(idBuf, sizeof(idBuf), "FDN%d", fdnIndex);
        instance.fdnId = idBuf;

        std::string suffix = "_fdn" + std::to_string(fdnIndex);

        // Create native drivers
        instance.loggerDriver = new NativeLoggerDriver(LOGGER_DRIVER_NAME + suffix);
        instance.loggerDriver->setSuppressOutput(getenv("PDN_CLI_LOG_FILE") == nullptr);
        instance.clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME + suffix);
        instance.displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        instance.primaryButtonDriver   = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME   + suffix, 0);
        instance.secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        instance.tertiaryButtonDriver  = new NativeButtonDriver(TERTIARY_BUTTON_DRIVER_NAME  + suffix, 2);
        instance.lightDriver     = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        instance.hapticsDriver   = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
        instance.serialInDriver           = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME           + suffix);
        instance.serialInSecondaryDriver  = new NativeSerialDriver(SERIAL_IN_SECONDARY_DRIVER_NAME + suffix);
        instance.httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        instance.httpClientDriver->setMockServerEnabled(true);
        instance.httpClientDriver->setConnected(true);
        instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        instance.storageDriver   = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);

        DriverConfig fdnConfig = {
            {DISPLAY_DRIVER_NAME,             instance.displayDriver},
            {PRIMARY_BUTTON_DRIVER_NAME,      instance.primaryButtonDriver},
            {SECONDARY_BUTTON_DRIVER_NAME,    instance.secondaryButtonDriver},
            {TERTIARY_BUTTON_DRIVER_NAME,     instance.tertiaryButtonDriver},
            {LIGHT_DRIVER_NAME,               instance.lightDriver},
            {HAPTICS_DRIVER_NAME,             instance.hapticsDriver},
            {SERIAL_IN_DRIVER_NAME,           instance.serialInDriver},
            {SERIAL_IN_SECONDARY_DRIVER_NAME, instance.serialInSecondaryDriver},
            {HTTP_CLIENT_DRIVER_NAME,         instance.httpClientDriver},
            {PEER_COMMS_DRIVER_NAME,          instance.peerCommsDriver},
            {PLATFORM_CLOCK_DRIVER_NAME,      instance.clockDriver},
            {LOGGER_DRIVER_NAME,              instance.loggerDriver},
            {STORAGE_DRIVER_NAME,             instance.storageDriver},
        };

        instance.fdn = FDN::createFDN(fdnConfig);
        instance.fdn->begin();
        instance.fdn->setDeviceId(instance.fdnId);

        // Create managers
        instance.remotePlayerManager = new RemotePlayerManager(instance.peerCommsDriver);

        instance.fdnConnectWirelessManager = new FDNConnectWirelessManager();
        instance.fdnConnectWirelessManager->initialize(instance.fdn->getWirelessManager());

        instance.symbolWirelessManager = new SymbolWirelessManager();
        instance.symbolWirelessManager->initialize(
            instance.fdn->getWirelessManager(),
            instance.fdn->getRemoteDeviceCoordinator());

        instance.controllerWirelessManager = new ControllerWirelessManager();
        instance.controllerWirelessManager->initialize(
            instance.fdn->getWirelessManager(),
            instance.fdn->getRemoteDeviceCoordinator());

        // Register ESP-NOW packet handlers (mirrors src/fdn/main.cpp setupEspNow)
        auto* peerComms = instance.peerCommsDriver;

        peerComms->setPacketHandler(
            PktType::kFdnConnect,
            [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
                static_cast<FDNConnectWirelessManager*>(arg)->processPacket(src, data, len);
            },
            instance.fdnConnectWirelessManager);

        peerComms->setPacketHandler(
            PktType::kSymbolMatchCommand,
            [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
                static_cast<SymbolWirelessManager*>(arg)->processSymbolMatchCommand(src, data, len);
            },
            instance.symbolWirelessManager);

        peerComms->setPacketHandler(
            PktType::kControllerCommand,
            [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
                static_cast<ControllerWirelessManager*>(arg)->processControllerCommand(src, data, len);
            },
            instance.controllerWirelessManager);

        peerComms->setPacketHandler(
            PktType::kGameSelect,
            [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
                static_cast<ControllerWirelessManager*>(arg)->processGameSelectCommand(src, data, len);
            },
            instance.controllerWirelessManager);

        peerComms->setPacketHandler(
            PktType::kGameResponse,
            [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
                static_cast<ControllerWirelessManager*>(arg)->processGameResponseCommand(src, data, len);
            },
            instance.controllerWirelessManager);

        // FDN does not receive peripheral commands (it sends them), but register a no-op
        // handler so the broker doesn't log "unknown packet type" warnings.
        peerComms->setPacketHandler(
            PktType::kPeripheralCommand,
            [](const uint8_t* /*src*/, const uint8_t* /*data*/, size_t /*len*/, void* /*arg*/) {},
            nullptr);

        // Apps — register both but launch directly into DemoModule for the simulator
        static constexpr bool kSymbolLockSingleSymbol = true;
        instance.symbolLockApp = new SymbolLock(
            instance.fdn,
            instance.symbolWirelessManager,
            instance.remotePlayerManager,
            kSymbolLockSingleSymbol);

        instance.demoModule = new DemoModule(
            DEMO_MODULE_APP_ID,
            instance.fdn->getRemoteDeviceCoordinator(),
            instance.controllerWirelessManager,
            /*skipDisconnectPolicy=*/true);

        AppConfig apps = {
            {StateId(SYMBOL_LOCK_APP_ID), instance.symbolLockApp},
            {StateId(DEMO_MODULE_APP_ID), instance.demoModule},
        };
        // Skip SymbolLock ceremony; launch straight into DemoModule.
        instance.fdn->loadAppConfig(apps, StateId(DEMO_MODULE_APP_ID));

        // Register with SerialCableBroker for cable simulation
        SerialCableBroker::getInstance().registerFDN(
            fdnIndex,
            instance.serialInDriver,
            instance.serialInSecondaryDriver);

        return instance;
    }

    static void destroyFDN(FDNInstance& instance) {
        SerialCableBroker::getInstance().unregisterFDN(instance.fdnIndex);
        delete instance.demoModule;
        delete instance.symbolLockApp;
        delete instance.controllerWirelessManager;
        delete instance.symbolWirelessManager;
        delete instance.fdnConnectWirelessManager;
        delete instance.remotePlayerManager;
        delete instance.fdn;
        // Drivers owned by FDN's DriverManager; deleted with fdn above.
    }
};

} // namespace cli

#endif // NATIVE_BUILD
