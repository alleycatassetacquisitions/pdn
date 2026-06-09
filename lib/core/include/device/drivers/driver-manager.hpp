#pragma once

#include "driver-interface.hpp"
#include <map>
#include <utility>
#include <functional>

using DriverConfig = std::map<std::string, DriverInterface*>;

// One uniform lifecycle for every peripheral (initialize() once, exec() every
// tick, deleted on dismount) so that a platform target is fully described by
// its DriverConfig and the rest of the system never branches on what hardware
// it runs on. exec() is each driver's cooperative time slice; drivers do their
// work in non-blocking increments there because nothing in this system may
// stall the loop. Registration transfers ownership: drivers are deleted by
// dismountDrivers(), so callers must not hold owning references.
class DriverManager {
    public:
    explicit DriverManager(const DriverConfig& config) : driverConfig(config) {}

    ~DriverManager() = default;

    int initialize() {
        for(auto& driver : driverConfig) {
            if(driver.second->initialize() != 0) {
                return 990 + static_cast<int>(driver.second->type); //Return 990 + driver type to indicate failure
            }
        }

        return 0;
    }

    void execDrivers() {
        for(auto& driver : driverConfig) {
            driver.second->exec();
        }
    }

    void dismountDrivers() {
        for(auto& driver : driverConfig) {
            delete driver.second;
        }
        driverConfig.clear();
    }

    DriverInterface* getDriver(const std::string& name) {
        auto it = driverConfig.find(name);
        return (it != driverConfig.end()) ? it->second : nullptr;
    }

private:
    DriverConfig driverConfig;
};