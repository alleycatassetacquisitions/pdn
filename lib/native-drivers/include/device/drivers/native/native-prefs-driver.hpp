#pragma once

#include "device/drivers/driver-interface.hpp"
#include <map>
#include <string>

/**
 * In-memory StorageInterface implementation for native/test builds.
 * Keys are stored as "namespace/key" composites so namespace isolation
 * mirrors the NVS behaviour of the ESP32 driver.
 */
class NativePrefsDriver : public StorageDriverInterface {
public:
    explicit NativePrefsDriver(const std::string& name) : StorageDriverInterface(name) {}

    ~NativePrefsDriver() override = default;

    int initialize() override { return 0; }
    void exec() override {}

    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        stringStorage_[ns + "/" + key] = value;
        return value.size();
    }

    std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) override {
        auto it = stringStorage_.find(ns + "/" + key);
        return it != stringStorage_.end() ? it->second : defaultValue;
    }

    bool remove(const std::string& ns, const std::string& key) override {
        const std::string composite = ns + "/" + key;
        auto strIt = stringStorage_.find(composite);
        if (strIt != stringStorage_.end()) {
            stringStorage_.erase(strIt);
            return true;
        }
        auto ucharIt = ucharStorage_.find(composite);
        if (ucharIt != ucharStorage_.end()) {
            ucharStorage_.erase(ucharIt);
            return true;
        }
        return false;
    }

    bool clear(const std::string& ns) override {
        const std::string prefix = ns + "/";
        for (auto it = stringStorage_.begin(); it != stringStorage_.end(); ) {
            it = it->first.rfind(prefix, 0) == 0 ? stringStorage_.erase(it) : ++it;
        }
        for (auto it = ucharStorage_.begin(); it != ucharStorage_.end(); ) {
            it = it->first.rfind(prefix, 0) == 0 ? ucharStorage_.erase(it) : ++it;
        }
        return true;
    }

    void end() override {}

    uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) override {
        auto it = ucharStorage_.find(ns + "/" + key);
        return it != ucharStorage_.end() ? it->second : defaultValue;
    }

    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        ucharStorage_[ns + "/" + key] = value;
        return 1;
    }

private:
    std::map<std::string, std::string> stringStorage_;
    std::map<std::string, uint8_t>     ucharStorage_;
};
