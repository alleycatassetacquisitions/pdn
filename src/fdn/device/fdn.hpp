#pragma once

#include "device/device.hpp"
#include "device/light-manager.hpp"
#include "device/serial-manager.hpp"

#include <string>

class FDN : public Device {

public:

    static FDN* createFDN(DriverConfig& driverConfig);

    ~FDN() override;

    int begin() override;

    void loop() override;

    void setDeviceId(const std::string& deviceId) override;

    std::string getDeviceId() override;
    DeviceType getDeviceType() override;

    LightManager* getLightManager() override;
    WirelessManager* getWirelessManager() override;
    SerialManager* getSerialManager() override;
    RemoteDeviceCoordinator* getRemoteDeviceCoordinator() override;

    // Convenience accessor for the tertiary (third) button unique to FDN.
    Button* getTertiaryButton();

protected:
    FDN(DriverConfig& driverConfig);

private:
    LightManager* lightManager;
    SerialManager* serialManager;
    WirelessManager* wirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    std::string deviceId;
};
