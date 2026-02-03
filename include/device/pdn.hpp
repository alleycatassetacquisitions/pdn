//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include "device.hpp"
#include "device/light-manager.hpp"

#include <string>

const std::string DISPLAY_DRIVER_NAME = "display";
const std::string PRIMARY_BUTTON_DRIVER_NAME = "primary_button";
const std::string SECONDARY_BUTTON_DRIVER_NAME = "secondary_button";
const std::string LIGHT_DRIVER_NAME = "light";
const std::string HAPTICS_DRIVER_NAME = "haptics";
const std::string SERIAL_OUT_DRIVER_NAME = "serial_out";
const std::string SERIAL_IN_DRIVER_NAME = "serial_in";
const std::string HTTP_CLIENT_DRIVER_NAME = "http_client";
const std::string PEER_COMMS_DRIVER_NAME = "peer_comms";
const std::string PLATFORM_CLOCK_DRIVER_NAME = "platform_clock";
const std::string LOGGER_DRIVER_NAME = "logger";
const std::string STORAGE_DRIVER_NAME = "storage";

class PDN : public Device {

public:

    static PDN* createPDN(DriverConfig& driverConfig);

    ~PDN() override;

    int begin() override;

    void loop() override;

    void onStateChange() override;

    void setDeviceId(const std::string& deviceId) override;

    std::string getDeviceId() override;

    Display* getDisplay() override;
    Haptics* getHaptics() override;
    Button* getPrimaryButton() override;
    Button* getSecondaryButton() override;
    LightManager* getLightManager() override;
    HttpClientInterface* getHttpClient() override;
    PeerCommsInterface* getPeerComms() override;
    StorageInterface* getStorage() override;
    WirelessManager* getWirelessManager() override;

protected:
    PDN(DriverConfig& driverConfig);

    HWSerialWrapper* outputJack();

    HWSerialWrapper* inputJack();

private:

    Display* display;
    Haptics* haptics;
    Button* primary;
    Button* secondary;
    LightStrip* pdnLights;
    LightManager* lightManager;

    HWSerialWrapper* serialOut;
    HWSerialWrapper* serialIn;

    HttpClientInterface* httpClient;
    PeerCommsInterface* peerComms;
    PlatformClock* platformClock;
    LoggerInterface* logger;
    StorageInterface* storage;
    WirelessManager* wirelessManager;

    std::string deviceId;
};
