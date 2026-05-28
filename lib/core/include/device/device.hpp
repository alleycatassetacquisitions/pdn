#pragma once

#include <string>
#include "drivers/display.hpp"
#include "drivers/button.hpp"
#include "drivers/haptics.hpp"
#include "drivers/http-client-interface.hpp"
#include "drivers/peer-comms-interface.hpp"
#include "drivers/storage-interface.hpp"
#include "drivers/light-interface.hpp"
#include "serial-manager.hpp"
#include "light-manager.hpp"
#include "drivers/driver-manager.hpp"
#include "remote-device-coordinator.hpp"
#include "drivers/logger.hpp"
#include "wireless-manager.hpp"
#include "state/state-types.hpp"
#include <map>
#include "device-type.hpp"
#include "driver-names.hpp"

class StateMachine;

using AppConfig = std::map<StateId, StateMachine*>;

class Device {
public:
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    virtual ~Device() {
        driverManager.dismountDrivers();
        appConfig.clear();
    }

    void loadAppConfig(AppConfig config, StateId launchAppId);

    virtual int begin() = 0;

    void setActiveApp(StateId appId);

    virtual void loop();

    virtual void setDeviceId(const std::string& deviceId) = 0;
    virtual std::string getDeviceId() = 0;
    virtual DeviceType getDeviceType() = 0;

    // Peripheral accessors with default implementations backed by DriverManager.
    // Subclasses no longer need to override these — they register drivers by name.
    // Virtual so mock subclasses in tests can still inject fakes directly.
    virtual Display*             getDisplay()         { return getPeripheral<Display>(DISPLAY_DRIVER_NAME); }
    virtual Haptics*             getHaptics()         { return getPeripheral<Haptics>(HAPTICS_DRIVER_NAME); }
    virtual Button*              getPrimaryButton()   { return getPeripheral<Button>(PRIMARY_BUTTON_DRIVER_NAME); }
    virtual Button*              getSecondaryButton() { return getPeripheral<Button>(SECONDARY_BUTTON_DRIVER_NAME); }
    virtual HttpClientInterface* getHttpClient()      { return getPeripheral<HttpClientInterface>(HTTP_CLIENT_DRIVER_NAME); }
    virtual PeerCommsInterface*  getPeerComms()       { return getPeripheral<PeerCommsInterface>(PEER_COMMS_DRIVER_NAME); }
    virtual StorageInterface*    getStorage()         { return getPeripheral<StorageInterface>(STORAGE_DRIVER_NAME); }

    // Manager accessors — these wrap higher-level objects constructed by subclasses
    // from multiple drivers and cannot be resolved purely by name lookup.
    virtual LightManager*             getLightManager() = 0;
    virtual WirelessManager*          getWirelessManager() = 0;
    virtual SerialManager*            getSerialManager() = 0;
    virtual RemoteDeviceCoordinator*  getRemoteDeviceCoordinator() = 0;

    // Generic driver accessor — useful for device-specific peripherals not
    // covered by the standard convenience wrappers (e.g. FDN tertiary button).
    // Uses abstractSelf() on each combined driver interface to perform the
    // sibling-base cross-cast at compile time, avoiding dynamic_cast/RTTI.
    template<typename T>
    T* getPeripheral(const std::string& name) {
        DriverInterface* targetDriver = driverManager.getDriver(name);
        return targetDriver ? static_cast<T*>(targetDriver->abstractSelf()) : nullptr;
    }

protected:
    explicit Device(const DriverConfig& deviceConfig) : driverManager(deviceConfig) {
        driverManager.initialize();
    }

private:
    DriverManager driverManager;
    AppConfig appConfig;
    StateId currentAppId;
};
