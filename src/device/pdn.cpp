#include "device/pdn.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"

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
    HWSerialWrapper* serialOut = static_cast<SerialDriverInterface*>(driverConfig[SERIAL_OUT_DRIVER_NAME]);
    HWSerialWrapper* serialIn = static_cast<SerialDriverInterface*>(driverConfig[SERIAL_IN_DRIVER_NAME]);
    httpClient = static_cast<HttpClientDriverInterface*>(driverConfig[HTTP_CLIENT_DRIVER_NAME]);
    peerComms = static_cast<PeerCommsDriverInterface*>(driverConfig[PEER_COMMS_DRIVER_NAME]);
    platformClock = static_cast<PlatformClockDriverInterface*>(driverConfig[PLATFORM_CLOCK_DRIVER_NAME]);
    logger = static_cast<LoggerDriverInterface*>(driverConfig[LOGGER_DRIVER_NAME]);
    storage = static_cast<StorageDriverInterface*>(driverConfig[STORAGE_DRIVER_NAME]);

    lightManager = new LightManager(*lights);
    serialManager = new SerialManager(serialOut, serialIn);
    wirelessManager = new WirelessManager(peerComms, httpClient);
    wirelessAppLauncher = new WirelessAppLauncher();
}

int PDN::begin() {
    // Initialize wireless manager to set up initial state
    wirelessManager->initialize();
    wirelessAppLauncher->initialize(wirelessManager);
    peerComms->setPacketHandler(
        PktType::kWirelessAppCommand,
        [](const uint8_t* srcAddr, const uint8_t* data, const size_t len, void* userArg) {
            ((WirelessAppLauncher*)userArg)->processAppCommand(srcAddr, data, len);
        },
        wirelessAppLauncher
    );

    wirelessAppLauncher->setPacketReceivedCallback(
        std::bind(&PDN::handleRemoteAppLaunchRequest, this, std::placeholders::_1)
    );
    
    return 1;
}

std::string PDN::getDeviceId() {
    return deviceId;
}

void PDN::loop() {
    Device::loop();
    lightManager->loop();
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

SerialManager* PDN::getSerialManager() {
    return serialManager;
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

WirelessManager* PDN::getWirelessManager() {
    return wirelessManager;
}

void PDN::setDeviceId(const std::string& id) {
    deviceId = id;
}

void PDN::handleRemoteAppLaunchRequest(const WirelessAppCommand& command) {
    if(command.command == AppCommand::APP_LAUNCH) {
        AppId appId = command.appId;
        setActiveApp(appId);
        wirelessAppLauncher->sendAppCommand(command.macAddress, AppCommand::APP_LAUNCH_ACK, appId);
    }
}
