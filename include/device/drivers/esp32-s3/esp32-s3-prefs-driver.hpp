#pragma once

#include "device/drivers/driver-interface.hpp"
#include <Preferences.h>

class Esp32S3PrefsDriver : public StorageDriverInterface {
public:
    Esp32S3PrefsDriver(const std::string& name, const std::string& prefsName) 
        : StorageDriverInterface(name), prefsName_(prefsName) {
        psramInit();
    }
        
    ~Esp32S3PrefsDriver() override = default;

    int initialize() override {
        if(!prefs_.begin(prefsName_.c_str(), false)) {
            return 1;
        }

        return 0;
    }
    
    void exec() override {
        // No periodic execution needed for preferences driver
    }
    
    size_t write(const std::string& key, const std::string& value) override {
        return prefs_.putString(key.c_str(), value.c_str());
    }
    
    std::string read(const std::string& key, std::string defaultValue) override {
        return std::string(prefs_.getString(key.c_str(), defaultValue.c_str()).c_str());
    }

    bool remove(const std::string& key) override {
        return prefs_.remove(key.c_str());
    }

    bool clear() override {
        return prefs_.clear();
    }

    void end() override {
        prefs_.end();
    }

    uint8_t readUChar(const std::string& key, uint8_t defaultValue) override {
        return prefs_.getUChar(key.c_str(), defaultValue);
    }

    size_t writeUChar(const std::string& key, uint8_t value) override {
        return prefs_.putUChar(key.c_str(), value);
    }

private:
    Preferences prefs_;
    std::string prefsName_;
};
