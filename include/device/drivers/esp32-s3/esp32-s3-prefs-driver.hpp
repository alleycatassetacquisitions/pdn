#pragma once

#include "device/drivers/driver-interface.hpp"
#include <Preferences.h>

class Esp32S3PrefsDriver : public StorageDriverInterface {
public:
    Esp32S3PrefsDriver(const std::string& name, const std::string& prefsNameParam) 
        : StorageDriverInterface(name), prefsName(prefsNameParam) {
        psramInit();
    }
        
    ~Esp32S3PrefsDriver() override = default;

    int initialize() override {
        if(!prefs.begin(prefsName.c_str(), false)) {
            return 1;
        }

        return 0;
    }
    
    void exec() override {
        // No periodic execution needed for preferences driver
    }
    
    size_t write(const std::string& key, const std::string& value) override {
        return prefs.putString(key.c_str(), value.c_str());
    }
    
    std::string read(const std::string& key, std::string defaultValue) override {
        return std::string(prefs.getString(key.c_str(), defaultValue.c_str()).c_str());
    }

    bool remove(const std::string& key) override {
        return prefs.remove(key.c_str());
    }

    bool clear() override {
        return prefs.clear();
    }

    void end() override {
        prefs.end();
    }

    uint8_t readUChar(const std::string& key, uint8_t defaultValue) override {
        return prefs.getUChar(key.c_str(), defaultValue);
    }

    size_t writeUChar(const std::string& key, uint8_t value) override {
        return prefs.putUChar(key.c_str(), value);
    }

private:
    Preferences prefs;
    std::string prefsName;
};
