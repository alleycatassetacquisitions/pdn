#pragma once

#include <string>
#include "drivers/display.hpp"
#include "drivers/button.hpp"
#include "drivers/light-interface.hpp"
#include "serial-manager.hpp"
#include "light-manager.hpp"
#include "drivers/driver-manager.hpp"
#include "drivers/logger.hpp"
#include "wireless-manager.hpp"
#include "state/state-types.hpp"
#include <map>

class StateMachine;

using AppConfig = std::map<StateId, StateMachine*>;

class Device {
public:
    // Delete copy and move operations (Rule of 5)
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

    virtual Display* getDisplay() = 0;
    virtual Haptics* getHaptics() = 0;
    virtual Button* getPrimaryButton() = 0;
    virtual Button* getSecondaryButton() = 0;
    virtual LightManager* getLightManager() = 0;
    virtual HttpClientInterface* getHttpClient() = 0;
    virtual PeerCommsInterface* getPeerComms() = 0;
    virtual StorageInterface* getStorage() = 0;
    virtual WirelessManager* getWirelessManager() = 0;
    virtual SerialManager* getSerialManager() = 0;

protected:
    explicit Device(const DriverConfig& deviceConfig) : driverManager(deviceConfig) {
        driverManager.initialize();
    }

private:
    DriverManager driverManager;
    AppConfig appConfig;
    StateId currentAppId;
};
