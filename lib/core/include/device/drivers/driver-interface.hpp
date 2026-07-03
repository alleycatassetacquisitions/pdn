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
#include "mqtt-interface.hpp"

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
    MQTT = 10,
};

class DriverInterface {
public:
    DriverType type;
    std::string name;
    DriverInterface(DriverType driverType, const std::string& driverName) : type(driverType), name(driverName) {}
    virtual ~DriverInterface() = default;
    virtual int initialize() = 0;
    virtual void exec() = 0;

    // Returns a pointer to the abstract interface side of this driver.
    // Each combined interface (XxxDriverInterface) overrides this to perform
    // the compile-time-known upcast, avoiding any need for dynamic_cast or RTTI.
    virtual void* abstractSelf() = 0;
};

class DisplayDriverInterface : public DriverInterface, public Display {
public:
    explicit DisplayDriverInterface(const std::string& name) : DriverInterface(DriverType::SCREEN, name) {}
    ~DisplayDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<Display*>(this); }
};

class ButtonDriverInterface : public DriverInterface, public Button {
public:
    explicit ButtonDriverInterface(const std::string& name) : DriverInterface(DriverType::BUTTON, name) {}
    ~ButtonDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<Button*>(this); }
};

class LightDriverInterface : public DriverInterface, public LightStrip {
public:
    explicit LightDriverInterface(const std::string& name) : DriverInterface(DriverType::LIGHT, name) {}
    ~LightDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<LightStrip*>(this); }
};

class HapticsMotorDriverInterface : public DriverInterface, public Haptics {
public:
    explicit HapticsMotorDriverInterface(const std::string& name) : DriverInterface(DriverType::HAPTICS, name) {}
    ~HapticsMotorDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<Haptics*>(this); }
};

class SerialDriverInterface : public DriverInterface, public HWSerialWrapper {
public:
    explicit SerialDriverInterface(const std::string& name) : DriverInterface(DriverType::SERIAL_JACK, name) {}
    ~SerialDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<HWSerialWrapper*>(this); }
};

class HttpClientDriverInterface : public DriverInterface, public HttpClientInterface {
public:
    explicit HttpClientDriverInterface(const std::string& name) : DriverInterface(DriverType::HTTP_CLIENT, name) {}
    ~HttpClientDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<HttpClientInterface*>(this); }
};

class PeerCommsDriverInterface : public DriverInterface, public PeerCommsInterface {
public:
    explicit PeerCommsDriverInterface(const std::string& name) : DriverInterface(DriverType::PEER_COMMS, name) {}
    ~PeerCommsDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<PeerCommsInterface*>(this); }
};

class PlatformClockDriverInterface : public DriverInterface, public PlatformClock {
public:
    explicit PlatformClockDriverInterface(const std::string& name) : DriverInterface(DriverType::PLATFORM_CLOCK, name) {}
    ~PlatformClockDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<PlatformClock*>(this); }
};

class LoggerDriverInterface : public DriverInterface, public LoggerInterface {
public:
    explicit LoggerDriverInterface(const std::string& name) : DriverInterface(DriverType::LOGGER, name) {}
    ~LoggerDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<LoggerInterface*>(this); }
};

class StorageDriverInterface : public DriverInterface, public StorageInterface {
public:
    explicit StorageDriverInterface(const std::string& name) : DriverInterface(DriverType::STORAGE, name) {}
    ~StorageDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<StorageInterface*>(this); }
};

class MQTTDriverInterface : public DriverInterface, public MQTTInterface {
public:
    explicit MQTTDriverInterface(const std::string& name) : DriverInterface(DriverType::MQTT, name) {}
    ~MQTTDriverInterface() override = default;
    void* abstractSelf() override { return static_cast<MQTTInterface*>(this); }
};