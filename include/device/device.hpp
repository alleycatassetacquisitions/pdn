#pragma once

#include <string>
#include "device-serial.hpp"
#include "drivers/display.hpp"
#include "drivers/button.hpp"
#include "drivers/light-interface.hpp"
#include "light-manager.hpp"
#include "drivers/driver-manager.hpp"
#include "wireless-manager.hpp"

class Device : public DeviceSerial {
public:
    // Delete copy and move operations (Rule of 5)
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    ~Device() override {
        driverManager.dismountDrivers();
    }

    virtual int begin() = 0;

    virtual void loop() {
        driverManager.execDrivers();
    }

    virtual void onStateChange() = 0;

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

protected:
    explicit Device(const DriverConfig& deviceConfig) : driverManager(deviceConfig) {
        driverManager.initialize();
    }

private:
    DriverManager driverManager;
};
