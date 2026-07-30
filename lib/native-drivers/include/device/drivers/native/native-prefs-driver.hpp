#pragma once

#include <map>
#include <set>
#include <string>
#include "device/drivers/driver-interface.hpp"
#include "device/drivers/unknown-storage-namespace-exception.hpp"

/**
 * In-memory StorageInterface for native / CLI builds.
 *
 * Mirrors Esp32S3PrefsDriver: namespaces must be registered at construction;
 * any operation on an unregistered name throws UnknownStorageNamespaceException.
 */
class NativePrefsDriver : public StorageDriverInterface {
public:
    NativePrefsDriver(const std::string& name, std::initializer_list<const char*> namespaces)
        : StorageDriverInterface(name) {
        for (const char* ns : namespaces) {
            registeredNamespaces.insert(std::string(ns));
        }
    }

    ~NativePrefsDriver() override = default;

    int initialize() override { return 0; }
    void exec() override {}

    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        requireRegisteredNamespace(ns);
        stringStorage_[compositeKey(ns, key)] = value;
        return value.size();
    }

    std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) override {
        requireRegisteredNamespace(ns);
        auto it = stringStorage_.find(compositeKey(ns, key));
        return it != stringStorage_.end() ? it->second : defaultValue;
    }

    bool remove(const std::string& ns, const std::string& key) override {
        requireRegisteredNamespace(ns);
        const std::string composite = compositeKey(ns, key);
        bool removed = false;
        if (stringStorage_.erase(composite) > 0) {
            removed = true;
        }
        if (ucharStorage_.erase(composite) > 0) {
            removed = true;
        }
        return removed;
    }

    bool clear(const std::string& ns) override {
        requireRegisteredNamespace(ns);
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
        requireRegisteredNamespace(ns);
        auto it = ucharStorage_.find(compositeKey(ns, key));
        return it != ucharStorage_.end() ? it->second : defaultValue;
    }

    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        requireRegisteredNamespace(ns);
        ucharStorage_[compositeKey(ns, key)] = value;
        return 1;
    }

private:
    static std::string compositeKey(const std::string& ns, const std::string& key) {
        return ns + "/" + key;
    }

    void requireRegisteredNamespace(const std::string& ns) const {
        if (registeredNamespaces.find(ns) == registeredNamespaces.end()) {
            throw UnknownStorageNamespaceException(ns);
        }
    }

    std::set<std::string> registeredNamespaces;
    std::map<std::string, std::string> stringStorage_;
    std::map<std::string, uint8_t> ucharStorage_;
};
