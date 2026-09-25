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
    /// Registers the namespaces this driver will accept; any other throws.
    NativePrefsDriver(const std::string& name, std::initializer_list<const char*> namespaces)
        : StorageDriverInterface(name) {
        for (const char* ns : namespaces) {
            registeredNamespaces.insert(std::string(ns));
        }
    }

    /// Nothing owned: both maps clean themselves up.
    ~NativePrefsDriver() override = default;

    /// Always succeeds; there is no backing store to open.
    int initialize() override { return 0; }
    /// No periodic work.
    void exec() override {}

    /// Stores a string under a registered namespace; returns its length.
    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        requireRegisteredNamespace(ns);
        stringStorage[compositeKey(ns, key)] = value;
        return value.size();
    }

    /// Reads a string back, or `defaultValue` when the key is unset.
    std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) override {
        requireRegisteredNamespace(ns);
        auto it = stringStorage.find(compositeKey(ns, key));
        return it != stringStorage.end() ? it->second : defaultValue;
    }

    /// Drops both the string and byte entries for one key.
    bool remove(const std::string& ns, const std::string& key) override {
        requireRegisteredNamespace(ns);
        const std::string composite = compositeKey(ns, key);
        bool removed = false;
        if (stringStorage.erase(composite) > 0) {
            removed = true;
        }
        if (ucharStorage.erase(composite) > 0) {
            removed = true;
        }
        return removed;
    }

    /// Drops every entry in one namespace.
    bool clear(const std::string& ns) override {
        requireRegisteredNamespace(ns);
        const std::string prefix = ns + "/";
        for (auto it = stringStorage.begin(); it != stringStorage.end(); ) {
            it = it->first.rfind(prefix, 0) == 0 ? stringStorage.erase(it) : ++it;
        }
        for (auto it = ucharStorage.begin(); it != ucharStorage.end(); ) {
            it = it->first.rfind(prefix, 0) == 0 ? ucharStorage.erase(it) : ++it;
        }
        return true;
    }

    /// No backing store to flush or close.
    void end() override {}

    /// Reads one byte back, or `defaultValue` when the key is unset.
    uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) override {
        requireRegisteredNamespace(ns);
        auto it = ucharStorage.find(compositeKey(ns, key));
        return it != ucharStorage.end() ? it->second : defaultValue;
    }

    /// Stores one byte under a registered namespace; throws if it is not one.
    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        requireRegisteredNamespace(ns);
        ucharStorage[compositeKey(ns, key)] = value;
        return 1;
    }

private:
    static std::string compositeKey(const std::string& ns, const std::string& key) {
        return ns + "/" + key;
    }

    void requireRegisteredNamespace(const std::string& ns) const {
        if (registeredNamespaces.find(ns) == registeredNamespaces.end()) {
            throw UnknownStorageNamespaceException(ns);  // catastrophic: caller used an unregistered namespace
        }
    }

    std::set<std::string> registeredNamespaces;
    std::map<std::string, std::string> stringStorage;
    std::map<std::string, uint8_t> ucharStorage;
};
