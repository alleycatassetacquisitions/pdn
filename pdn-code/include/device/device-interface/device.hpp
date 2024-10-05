#pragma once

#include "device-peripherals.hpp"

class Device {
public:

    virtual int begin() = 0;

    virtual void tick() = 0;

    String* getDeviceId() {
        return &deviceId;
    };

    void setDeviceId(String* deviceId) {
        this->deviceId = *deviceId;
    }

    virtual Peripherals getPeripherals() = 0;

protected:
    Device();
    String deviceId = "";
    virtual void initializePins() = 0;
};
