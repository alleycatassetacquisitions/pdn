#pragma once

#include <string>
#include <vector>
#include <utility>
#include "device/drivers/driver-interface.hpp"
#include <Preferences.h>

/**
 * NVS-backed storage driver for ESP32-S3.
 *
 * Constructed with a list of namespace names; the first entry is the primary
 * namespace. All read/write operations require an explicit namespace argument.
 *
 * Usage:
 *   new Esp32S3PrefsDriver(STORAGE_DRIVER_NAME, {PREF_NAMESPACE, CRASH_LOG_NAMESPACE})
 */
class Esp32S3PrefsDriver : public StorageDriverInterface {
public:
    Esp32S3PrefsDriver(const std::string& name, std::initializer_list<const char*> namespaces)
        : StorageDriverInterface(name) {
        psramInit();
        bool isFirst = true;
        for (const char* ns : namespaces) {
            if (isFirst) {
                primaryNamespace = ns;
                isFirst = false;
            } else {
                extraPrefs.push_back({std::string(ns), new Preferences()});
            }
        }
    }

    ~Esp32S3PrefsDriver() override {
        for (auto& entry : extraPrefs) {
            entry.second->end();
            delete entry.second;
        }
    }

    int initialize() override {
        if (!prefs.begin(primaryNamespace.c_str(), false)) {
            return 1;
        }
        for (auto& entry : extraPrefs) {
            if (!entry.second->begin(entry.first.c_str(), false)) {
                return 1;
            }
        }
        return 0;
    }

    void exec() override {}

    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        if (Preferences* p = findPrefs(ns)) return p->putString(key.c_str(), value.c_str());
        return 0;
    }

    std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) override {
        if (Preferences* p = findPrefs(ns)) {
            return std::string(p->getString(key.c_str(), defaultValue.c_str()).c_str());
        }
        return defaultValue;
    }

    bool remove(const std::string& ns, const std::string& key) override {
        if (Preferences* p = findPrefs(ns)) return p->remove(key.c_str());
        return false;
    }

    bool clear(const std::string& ns) override {
        if (Preferences* p = findPrefs(ns)) return p->clear();
        return false;
    }

    void end() override {
        prefs.end();
        for (auto& entry : extraPrefs) {
            entry.second->end();
        }
    }

    uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) override {
        if (Preferences* p = findPrefs(ns)) return p->getUChar(key.c_str(), defaultValue);
        return defaultValue;
    }

    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        if (Preferences* p = findPrefs(ns)) return p->putUChar(key.c_str(), value);
        return 0;
    }

private:
    Preferences* findPrefs(const std::string& ns) {
        if (ns == primaryNamespace) return &prefs;
        for (auto& entry : extraPrefs) {
            if (entry.first == ns) return entry.second;
        }
        return nullptr;
    }

    std::string primaryNamespace;
    Preferences prefs;
    std::vector<std::pair<std::string, Preferences*>> extraPrefs;
};
