#pragma once

#include <string>
#include "device-serial.hpp"
#include "drivers/display.hpp"
#include "drivers/button.hpp"
#include "drivers/light-interface.hpp"
#include "light-manager.hpp"
#include "drivers/driver-manager.hpp"

class Device : public DeviceSerial {
public:

    ~Device() override {
        driverManager.dismountDrivers();
    }

    virtual int begin() = 0;

    virtual void loop() {
        driverManager.execDrivers();
    }

    virtual void onStateChange() = 0;

    virtual void setDeviceId(std::string deviceId) = 0;

    virtual std::string getDeviceId() = 0;

    virtual Display* getDisplay() = 0;
    virtual Haptics* getHaptics() = 0;
    virtual Button* getPrimaryButton() = 0;
    virtual Button* getSecondaryButton() = 0;
    virtual LightManager* getLightManager() = 0;
    virtual HttpClientInterface* getHttpClient() = 0;
    virtual PeerCommsInterface* getPeerComms() = 0;
    virtual StorageInterface* getStorage() = 0;

protected:

    Device(DriverConfig deviceConfig) : driverManager(deviceConfig) {
        driverManager.initialize();
    }

    DriverManager driverManager;
};
