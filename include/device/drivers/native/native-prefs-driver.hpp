#pragma once

#include "device/drivers/driver-interface.hpp"
#include <map>
#include <string>

class NativePrefsDriver : public StorageDriverInterface {
public:
    explicit NativePrefsDriver(const std::string& name) : StorageDriverInterface(name) {}

    ~NativePrefsDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {}

    size_t write(const std::string& key, const std::string& value) override {
        stringStorage_[key] = value;
        return value.size();
    }

    std::string read(const std::string& key, std::string defaultValue) override {
        auto it = stringStorage_.find(key);
        if (it != stringStorage_.end()) {
            return it->second;
        }
        return defaultValue;
    }

    bool remove(const std::string& key) override {
        auto strIt = stringStorage_.find(key);
        if (strIt != stringStorage_.end()) {
            stringStorage_.erase(strIt);
            return true;
        }
        auto ucharIt = ucharStorage_.find(key);
        if (ucharIt != ucharStorage_.end()) {
            ucharStorage_.erase(ucharIt);
            return true;
        }
        return false;
    }

    bool clear() override {
        stringStorage_.clear();
        ucharStorage_.clear();
        return true;
    }

    void end() override {
        // No-op for native - data is in memory only
    }

    uint8_t readUChar(const std::string& key, uint8_t defaultValue) override {
        auto it = ucharStorage_.find(key);
        if (it != ucharStorage_.end()) {
            return it->second;
        }
        return defaultValue;
    }

    size_t writeUChar(const std::string& key, uint8_t value) override {
        ucharStorage_[key] = value;
        return 1;
    }

private:
    std::map<std::string, std::string> stringStorage_;
    std::map<std::string, uint8_t> ucharStorage_;
};
