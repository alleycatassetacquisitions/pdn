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
#include "wireless/quickdraw-wireless-manager.hpp"

namespace cli {

/**
 * Get human-readable name for a Quickdraw state ID.
 * TODO: Eventually move this to the State class itself.
 */
inline const char* getStateName(int stateId) {
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
    Player* player = nullptr;
    Quickdraw* game = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
    
    // State history (circular buffer, most recent at back)
    std::deque<int> stateHistory;
    int lastStateId = -1;
    
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
        instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        instance.storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);
        
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
        
        // Create game (no remote debug manager for now)
        instance.game = new Quickdraw(instance.player, instance.pdn, instance.quickdrawWirelessManager, nullptr);
        instance.game->initialize();
        
        return instance;
    }
    
    /**
     * Clean up a device instance and free all resources.
     */
    static void destroyDevice(DeviceInstance& device) {
        delete device.game;
        delete device.quickdrawWirelessManager;
        delete device.player;
        delete device.pdn;
        // Note: drivers are owned by DriverManager via PDN, so they're deleted when PDN is deleted
    }
};

} // namespace cli

#endif // NATIVE_BUILD
