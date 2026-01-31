#pragma once

#include "driver-interface.hpp"
#include <map>
#include <utility>
#include <functional>

using DriverConfig = std::map<std::string, DriverInterface*, std::less<>>;

class DriverManager {
    public:
    explicit DriverManager(const DriverConfig& config) : driverConfig(config) {}

    ~DriverManager() = default;

    int initialize() {
        for(auto& [driverName, driverPtr] : driverConfig) {
            if(driverPtr->initialize() != 0) {
                return 990 + static_cast<int>(driverPtr->type); //Return 990 + driver type to indicate failure
            }
        }

        return 0;
    }

    void execDrivers() {
        for(auto& [driverName, driverPtr] : driverConfig) {
            driverPtr->exec();
        }
    }

    void dismountDrivers() {
        for(auto& [driverName, driverPtr] : driverConfig) {
            delete driverPtr;
        }
        driverConfig.clear();
    }

private:
    DriverConfig driverConfig;
};