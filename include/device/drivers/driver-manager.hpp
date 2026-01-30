#pragma once

#include "driver-interface.hpp"
#include <map>
#include <utility>
#include <functional>

using DriverConfig = std::map<std::string, DriverInterface*, std::less<>>;

class DriverManager {
    public:
    explicit DriverManager(const DriverConfig& driverConfig) : driverConfig_(driverConfig) {}

    ~DriverManager() = default;

    int initialize() {
        for(auto& driver : driverConfig_) {
            if(driver.second->initialize() != 0) {
                return 990 + static_cast<int>(driver.second->type); //Return 990 + driver type to indicate failure
            }
        }

        return 0;
    }

    void execDrivers() {
        for(auto& driver : driverConfig_) {
            driver.second->exec();
        }
    }

    void dismountDrivers() {
        for(auto& driver : driverConfig_) {
            delete driver.second;
        }
        driverConfig_.clear();
    }

    private:
    DriverConfig driverConfig_;
};