#pragma once

#include <map>
#include <string>
#include "device/drivers/driver-interface.hpp"
#include "device/drivers/unknown-storage-namespace-exception.hpp"
#include <Preferences.h>

/**
 * NVS-backed storage driver for ESP32-S3.
 *
 * Constructed with a list of namespace names; each opens a separate NVS partition
 * namespace on initialize(). All read/write operations require an explicit namespace
 * argument. Using a namespace not passed to the constructor throws
 * UnknownStorageNamespaceException.
 *
 * Usage:
 *   new Esp32S3PrefsDriver(STORAGE_DRIVER_NAME, {MATCHES_PREFS_NAMESPACE, CRASH_LOG_NAMESPACE})
 */
class Esp32S3PrefsDriver : public StorageDriverInterface {
public:
    Esp32S3PrefsDriver(const std::string& name, std::initializer_list<const char*> namespaces)
        : StorageDriverInterface(name) {
        psramInit();
        for (const char* ns : namespaces) {
            namespacePrefs.try_emplace(std::string(ns), Preferences{});
        }
    }

    ~Esp32S3PrefsDriver() override {
        endAllNamespaces();
    }

    int initialize() override {
        for (auto& entry : namespacePrefs) {
            if (!entry.second.begin(entry.first.c_str(), false)) {
                return 1;
            }
        }
        return 0;
    }

    void exec() override {}

    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        return requirePrefs(ns)->putString(key.c_str(), value.c_str());
    }

    std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) override {
        return std::string(requirePrefs(ns)->getString(key.c_str(), defaultValue.c_str()).c_str());
    }

    bool remove(const std::string& ns, const std::string& key) override {
        return requirePrefs(ns)->remove(key.c_str());
    }

    bool clear(const std::string& ns) override {
        return requirePrefs(ns)->clear();
    }

    void end() override {
        endAllNamespaces();
    }

    uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) override {
        return requirePrefs(ns)->getUChar(key.c_str(), defaultValue);
    }

    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        return requirePrefs(ns)->putUChar(key.c_str(), value);
    }

private:
    Preferences* requirePrefs(const std::string& ns) {
        Preferences* p = findPrefs(ns);
        if (p == nullptr) {
            throw UnknownStorageNamespaceException(ns);
        }
        return p;
    }

    Preferences* findPrefs(const std::string& ns) {
        auto it = namespacePrefs.find(ns);
        if (it == namespacePrefs.end()) {
            return nullptr;
        }
        return &it->second;
    }

    void endAllNamespaces() {
        for (auto& entry : namespacePrefs) {
            entry.second.end();
        }
    }

    std::map<std::string, Preferences> namespacePrefs;
};
