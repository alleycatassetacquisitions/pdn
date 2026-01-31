#pragma once

#include "driver-interface.hpp"
#include <map>
#include <utility>
#include <functional>

using DriverConfig = std::map<std::string, DriverInterface*>;

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

private:
    DriverConfig driverConfig;
};