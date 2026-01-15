#include "device/pdn.hpp"
#include <Arduino.h> //TODO: Remove by creating storage driver.
#include "logger.hpp"

PDN* PDN::createPDN(DriverConfig& driverConfig) {
    return new PDN(driverConfig);
}

PDN::~PDN() {
}

PDN::PDN(DriverConfig& driverConfig) : Device(driverConfig) {
    display = static_cast<DisplayDriverInterface*>(driverConfig[DISPLAY_DRIVER_NAME]);
    primary = static_cast<ButtonDriverInterface*>(driverConfig[PRIMARY_BUTTON_DRIVER_NAME]);
    secondary = static_cast<ButtonDriverInterface*>(driverConfig[SECONDARY_BUTTON_DRIVER_NAME]);
    LightStrip* lights = static_cast<LightDriverInterface*>(driverConfig[LIGHT_DRIVER_NAME]);
    haptics = static_cast<HapticsMotorDriverInterface*>(driverConfig[HAPTICS_DRIVER_NAME]);
    serialOut = static_cast<SerialDriverInterface*>(driverConfig[SERIAL_OUT_DRIVER_NAME]);
    serialIn = static_cast<SerialDriverInterface*>(driverConfig[SERIAL_IN_DRIVER_NAME]);
    httpClient = static_cast<HttpClientDriverInterface*>(driverConfig[HTTP_CLIENT_DRIVER_NAME]);
    peerComms = static_cast<PeerCommsDriverInterface*>(driverConfig[PEER_COMMS_DRIVER_NAME]);
    platformClock = static_cast<PlatformClockDriverInterface*>(driverConfig[PLATFORM_CLOCK_DRIVER_NAME]);
    logger = static_cast<LoggerDriverInterface*>(driverConfig[LOGGER_DRIVER_NAME]);

    lightManager = new LightManager(*lights);
}

int PDN::begin() {
    return 1;
}

std::string PDN::getDeviceId() {
    return deviceId;
}

HWSerialWrapper* PDN::outputJack() {
    return serialOut;
}

HWSerialWrapper* PDN::inputJack() {
    return serialIn;
}

void PDN::loop() {
    Device::loop();
    lightManager->loop();
}

void PDN::onStateChange() {
    lightManager->clear(LightIdentifier::DISPLAY_LIGHTS);
    lightManager->clear(LightIdentifier::GRIP_LIGHTS);
    flushSerial();
}

Display* PDN::getDisplay() {
    return display;
}

Haptics* PDN::getHaptics() {
    return haptics;
}

Button* PDN::getPrimaryButton() {
    return primary;
}

Button* PDN::getSecondaryButton() {
    return secondary;
}

LightManager* PDN::getLightManager() {
    return lightManager;
}

HttpClientInterface* PDN::getHttpClient() {
    return httpClient;
}

PeerCommsInterface* PDN::getPeerComms() {
    return peerComms;
}

StorageInterface* PDN::getStorage() {
    return storage;
}

void PDN::setDeviceId(std::string id) {
    deviceId = id;
}
