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

    // Every driver is given its chance regardless of what failed before it.
    // driverConfig is a map, so the order is its keys' -- alphabetical, chosen by
    // nobody -- and returning early made one peripheral's failure silently skip
    // every driver whose name sorts after it.
    int initialize() {
        int failures = 0;
        for(auto& driver : driverConfig) {
            if(driver.second->initialize() != 0) {
                LOG_E("DRV", "%s failed to initialize", driver.first.c_str());
                failures++;
            }
        }

        return failures;
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