#pragma once

#include "device/device.hpp"
#include "device/light-manager.hpp"
#include "device/serial-manager.hpp"

#include <string>

class PDN : public Device {

public:

    static PDN* createPDN(DriverConfig& driverConfig);

    ~PDN() override;

    int begin() override;

    void loop() override;

    void setDeviceId(const std::string& deviceId) override;

    std::string getDeviceId() override;
    DeviceType getDeviceType() override;

    LightManager* getLightManager() override;
    WirelessManager* getWirelessManager() override;
    SerialManager* getSerialManager() override;
    RemoteDeviceCoordinator* getRemoteDeviceCoordinator() override;

protected:
    PDN(DriverConfig& driverConfig);

    // No-arg constructor for test subclasses that override all virtual getters.
    // Skips driver initialization — do not call in production code.
    PDN() : Device(DriverConfig{}) {
        lightManager = nullptr;
        serialManager = nullptr;
        wirelessManager = nullptr;
        remoteDeviceCoordinator = nullptr;
    }

private:
    LightManager* lightManager;
    SerialManager* serialManager;
    WirelessManager* wirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    std::string deviceId;
};
