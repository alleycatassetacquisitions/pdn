#pragma once

#include "display.hpp"
#include "button.hpp"
#include "light-interface.hpp"
#include "haptics.hpp"
#include "serial-wrapper.hpp"
#include "http-client-interface.hpp"
#include "peer-comms-interface.hpp"
#include "logger.hpp"
#include "platform-clock.hpp"
#include "storage-interface.hpp"

enum class DriverType {
    SCREEN = 0,
    BUTTON = 1,
    LIGHT = 2,
    HAPTICS = 3,
    SERIAL_JACK = 4,
    HTTP_CLIENT = 5,
    PEER_COMMS = 6,
    PLATFORM_CLOCK = 7,
    LOGGER = 8,
    STORAGE = 9,
};

class DriverInterface {
public:
    DriverType type;
    std::string name;
    DriverInterface(DriverType driverType, const std::string& driverName) : type(driverType), name(driverName) {}
    virtual ~DriverInterface() = default;
    virtual int initialize() = 0; //Returns 0 on success, -1 on failure
    virtual void exec() = 0;
};

class DisplayDriverInterface : public DriverInterface, public Display {
public:
    explicit DisplayDriverInterface(const std::string& name) : DriverInterface(DriverType::SCREEN, name) {}
    ~DisplayDriverInterface() override = default;
};

class ButtonDriverInterface : public DriverInterface, public Button {
public:
    explicit ButtonDriverInterface(const std::string& name) : DriverInterface(DriverType::BUTTON, name) {}
    ~ButtonDriverInterface() override = default;
};

class LightDriverInterface : public DriverInterface, public LightStrip {
public:
    explicit LightDriverInterface(const std::string& name) : DriverInterface(DriverType::LIGHT, name) {}
    ~LightDriverInterface() override = default;
};

class HapticsMotorDriverInterface : public DriverInterface, public Haptics {
public:
    explicit HapticsMotorDriverInterface(const std::string& name) : DriverInterface(DriverType::HAPTICS, name) {}
    ~HapticsMotorDriverInterface() override = default;
};

class SerialDriverInterface : public DriverInterface, public HWSerialWrapper {
public:
    explicit SerialDriverInterface(const std::string& name) : DriverInterface(DriverType::SERIAL_JACK, name) {}
    ~SerialDriverInterface() override = default;
};

class HttpClientDriverInterface : public DriverInterface, public HttpClientInterface {
public:
    explicit HttpClientDriverInterface(const std::string& name) : DriverInterface(DriverType::HTTP_CLIENT, name) {}
    ~HttpClientDriverInterface() override = default;
};

class PeerCommsDriverInterface : public DriverInterface, public PeerCommsInterface {
public:
    explicit PeerCommsDriverInterface(const std::string& name) : DriverInterface(DriverType::PEER_COMMS, name) {}
    ~PeerCommsDriverInterface() override = default;
};

class PlatformClockDriverInterface : public DriverInterface, public PlatformClock {
public:
    explicit PlatformClockDriverInterface(const std::string& name) : DriverInterface(DriverType::PLATFORM_CLOCK, name) {}
    ~PlatformClockDriverInterface() override = default;
};

class LoggerDriverInterface : public DriverInterface, public LoggerInterface {
public:
    explicit LoggerDriverInterface(const std::string& name) : DriverInterface(DriverType::LOGGER, name) {}
    ~LoggerDriverInterface() override = default;
};

class StorageDriverInterface : public DriverInterface, public StorageInterface {
public:
    explicit StorageDriverInterface(const std::string& name) : DriverInterface(DriverType::STORAGE, name) {}
    ~StorageDriverInterface() override = default;
};