#pragma once

#include <map>
#include <string>
#include <vector>
#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/unknown-storage-namespace-exception.hpp"

#include <nvs.h>
#include <nvs_flash.h>

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
        for (const char* ns : namespaces) {
            namespaceHandles.try_emplace(std::string(ns), 0);
        }
    }

    ~Esp32S3PrefsDriver() override {
        endAllNamespaces();
    }

    int initialize() override {
        // Preferences::begin() did this implicitly on first use. Raw NVS does
        // not, and a namespace cannot be opened before the partition is
        // initialised.
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        if (err != ESP_OK) {
            LOG_E(name.c_str(), "nvs_flash_init failed: %d", static_cast<int>(err));
            return 1;
        }

        for (auto& entry : namespaceHandles) {
            if (nvs_open(entry.first.c_str(), NVS_READWRITE, &entry.second) != ESP_OK) {
                LOG_E(name.c_str(), "nvs_open failed for namespace %s", entry.first.c_str());
                return 1;
            }
        }
        return 0;
    }

    void exec() override {}

    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        nvs_handle_t handle = requireHandle(ns);
        if (nvs_set_str(handle, key.c_str(), value.c_str()) != ESP_OK) {
            return 0;
        }
        // Preferences committed on every put; callers depend on a write being
        // durable as soon as it returns.
        if (nvs_commit(handle) != ESP_OK) {
            return 0;
        }
        return value.size();
    }

    std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) override {
        nvs_handle_t handle = requireHandle(ns);
        size_t length = 0;
        if (nvs_get_str(handle, key.c_str(), nullptr, &length) != ESP_OK || length == 0) {
            return defaultValue;
        }
        std::vector<char> buffer(length);
        if (nvs_get_str(handle, key.c_str(), buffer.data(), &length) != ESP_OK) {
            return defaultValue;
        }
        return std::string(buffer.data());
    }

    bool remove(const std::string& ns, const std::string& key) override {
        nvs_handle_t handle = requireHandle(ns);
        if (nvs_erase_key(handle, key.c_str()) != ESP_OK) {
            return false;
        }
        return nvs_commit(handle) == ESP_OK;
    }

    bool clear(const std::string& ns) override {
        nvs_handle_t handle = requireHandle(ns);
        if (nvs_erase_all(handle) != ESP_OK) {
            return false;
        }
        return nvs_commit(handle) == ESP_OK;
    }

    void end() override {
        endAllNamespaces();
    }

    uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) override {
        nvs_handle_t handle = requireHandle(ns);
        uint8_t value = 0;
        if (nvs_get_u8(handle, key.c_str(), &value) != ESP_OK) {
            return defaultValue;
        }
        return value;
    }

    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        nvs_handle_t handle = requireHandle(ns);
        if (nvs_set_u8(handle, key.c_str(), value) != ESP_OK) {
            return 0;
        }
        if (nvs_commit(handle) != ESP_OK) {
            return 0;
        }
        return sizeof(value);
    }

private:
    nvs_handle_t requireHandle(const std::string& ns) {
        auto it = namespaceHandles.find(ns);
        if (it == namespaceHandles.end()) {
            throw UnknownStorageNamespaceException(ns);
        }
        return it->second;
    }

    void endAllNamespaces() {
        for (auto& entry : namespaceHandles) {
            if (entry.second != 0) {
                nvs_close(entry.second);
                entry.second = 0;
            }
        }
    }

    std::map<std::string, nvs_handle_t> namespaceHandles;
};
