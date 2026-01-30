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
    DriverInterface(DriverType type, std::string name) : type(type), name(name) {}
    virtual ~DriverInterface() = default;
    virtual int initialize() = 0; //Returns 0 on success, -1 on failure
    virtual void exec() = 0;
};

class DisplayDriverInterface : public DriverInterface, public Display {
public:
    DisplayDriverInterface(std::string name) : DriverInterface(DriverType::SCREEN, name) {}
    virtual ~DisplayDriverInterface() = default;
};

class ButtonDriverInterface : public DriverInterface, public Button {
public:
    ButtonDriverInterface(std::string name) : DriverInterface(DriverType::BUTTON, name) {}
    virtual ~ButtonDriverInterface() = default;
};

class LightDriverInterface : public DriverInterface, public LightStrip {
public:
    LightDriverInterface(std::string name) : DriverInterface(DriverType::LIGHT, name) {}
    virtual ~LightDriverInterface() = default;
};

class HapticsMotorDriverInterface : public DriverInterface, public Haptics {
public:
    HapticsMotorDriverInterface(std::string name) : DriverInterface(DriverType::HAPTICS, name) {}
    virtual ~HapticsMotorDriverInterface() = default;
};

class SerialDriverInterface : public DriverInterface, public HWSerialWrapper {
public:
    SerialDriverInterface(std::string name) : DriverInterface(DriverType::SERIAL_JACK, name) {}
    virtual ~SerialDriverInterface() = default;
};

class HttpClientDriverInterface : public DriverInterface, public HttpClientInterface {
public:
    HttpClientDriverInterface(std::string name) : DriverInterface(DriverType::HTTP_CLIENT, name) {}
    virtual ~HttpClientDriverInterface() = default;
};

class PeerCommsDriverInterface : public DriverInterface, public PeerCommsInterface {
public:
    PeerCommsDriverInterface(std::string name) : DriverInterface(DriverType::PEER_COMMS, name) {}
    virtual ~PeerCommsDriverInterface() = default;
};

class PlatformClockDriverInterface : public DriverInterface, public PlatformClock {
public:
    PlatformClockDriverInterface(std::string name) : DriverInterface(DriverType::PLATFORM_CLOCK, name) {}
    virtual ~PlatformClockDriverInterface() = default;
};

class LoggerDriverInterface : public DriverInterface, public LoggerInterface {
public:
    LoggerDriverInterface(std::string name) : DriverInterface(DriverType::LOGGER, name) {}
    virtual ~LoggerDriverInterface() = default;
};

class StorageDriverInterface : public DriverInterface, public StorageInterface {
public:
    StorageDriverInterface(std::string name) : DriverInterface(DriverType::STORAGE, name) {}
    virtual ~StorageDriverInterface() = default;
};