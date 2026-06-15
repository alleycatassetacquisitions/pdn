#include "device/fdn.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/driver-names.hpp"
#include "device/drivers/logger.hpp"

FDN* FDN::createFDN(DriverConfig& driverConfig) {
    return new FDN(driverConfig);
}

FDN::~FDN() {}

FDN::FDN(DriverConfig& driverConfig) : Device(driverConfig) {
    lightManager = new FDNLightManager(
        *getPeripheral<LightStrip>(LIGHT_DRIVER_NAME));

    // FDN has two input jacks and no output jack.
    // SerialManager is constructed with nullptr for the output jack.
    auto* primaryIn   = getPeripheral<HWSerialWrapper>(SERIAL_IN_DRIVER_NAME);
    auto* secondaryIn = getPeripheral<HWSerialWrapper>(SERIAL_IN_SECONDARY_DRIVER_NAME);

    serialManager = new SerialManager(/*outputJack=*/nullptr, primaryIn);
    serialManager->setSecondaryInputJack(secondaryIn);

    wirelessManager = new WirelessManager(getPeerComms(), getHttpClient());
    remoteDeviceCoordinator = new RemoteDeviceCoordinator();
}

int FDN::begin() {
    wirelessManager->initialize();
    remoteDeviceCoordinator->initialize(wirelessManager, serialManager, this);
    return 1;
}

std::string FDN::getDeviceId() {
    return deviceId;
}

DeviceType FDN::getDeviceType() {
    return DeviceType::FDN;
}

void FDN::loop() {
    Device::loop();
    lightManager->loop();
    if (remoteDeviceCoordinator) {
        remoteDeviceCoordinator->sync(this);
    }
}

LightManager* FDN::getLightManager() {
    return lightManager;
}

SerialManager* FDN::getSerialManager() {
    return serialManager;
}

WirelessManager* FDN::getWirelessManager() {
    return wirelessManager;
}

RemoteDeviceCoordinator* FDN::getRemoteDeviceCoordinator() {
    return remoteDeviceCoordinator;
}

Button* FDN::getTertiaryButton() {
    return getPeripheral<Button>(TERTIARY_BUTTON_DRIVER_NAME);
}

void FDN::setDeviceId(const std::string& id) {
    deviceId = id;
}
