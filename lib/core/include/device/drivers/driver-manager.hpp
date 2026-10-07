#pragma once

#include "driver-interface.hpp"
#include "logger.hpp"
#include <map>
#include <utility>
#include <functional>

using DriverConfig = std::map<std::string, DriverInterface*>;

class DriverManager {
    public:
    explicit DriverManager(const DriverConfig& config) : driverConfig(config) {}

    ~DriverManager() = default;

    /**
     * Initializes every registered driver, logging the ones that fail.
     *
     * driverConfig is a map, so init order is alphabetical. Every driver is given
     * its chance regardless of what failed before it: a dead peripheral must not
     * silently take its neighbours with it.
     */
    void initialize() {
        for(auto& driver : driverConfig) {
            if(driver.second->initialize() != 0) {
                LOG_E("DRV", "%s failed to initialize", driver.first.c_str());
            }
        }
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