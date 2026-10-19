#include "device/pdn.hpp"
#include "device/driver-names.hpp"
#include "device/drivers/logger.hpp"
#include "device/firmware-update-manager.hpp"

PDN* PDN::createPDN(DriverConfig& driverConfig) {
    return new PDN(driverConfig);
}

PDN::~PDN() {
    delete firmwareUpdateManager;
}

PDN::PDN(DriverConfig& driverConfig) : Device(driverConfig) {
    lightManager = new LightManager(
        *getPeripheral<LightStrip>(LIGHT_DRIVER_NAME));
    serialManager = new SerialManager(
        getPeripheral<HWSerialWrapper>(SERIAL_OUT_DRIVER_NAME),
        getPeripheral<HWSerialWrapper>(SERIAL_IN_DRIVER_NAME));
    wirelessManager = new WirelessManager(getPeerComms(), getHttpClient());
    remoteDeviceCoordinator = new RemoteDeviceCoordinator();
    FirmwareStoreInterface* firmwareStore = getPeripheral<FirmwareStoreInterface>(FIRMWARE_STORE_DRIVER_NAME);
    if (firmwareStore != nullptr) {
        firmwareUpdateManager = new FirmwareUpdateManager(getPeerComms(), firmwareStore);
    }
}

int PDN::begin() {
    wirelessManager->initialize();
    remoteDeviceCoordinator->initialize(wirelessManager, serialManager, this);
    return 1;
}

std::string PDN::getDeviceId() {
    return deviceId;
}

DeviceType PDN::getDeviceType() {
    return DeviceType::PDN;
}

void PDN::loop() {
    Device::loop();
    lightManager->loop();
    if (remoteDeviceCoordinator) {
        remoteDeviceCoordinator->sync(this);
    }
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->sync();
    }
}

LightManager* PDN::getLightManager() {
    return lightManager;
}

SerialManager* PDN::getSerialManager() {
    return serialManager;
}

WirelessManager* PDN::getWirelessManager() {
    return wirelessManager;
}

RemoteDeviceCoordinator* PDN::getRemoteDeviceCoordinator() {
    return remoteDeviceCoordinator;
}

FirmwareUpdateManager* PDN::getFirmwareUpdateManager() {
    return firmwareUpdateManager;
}

void PDN::setDeviceId(const std::string& id) {
    deviceId = id;
}
