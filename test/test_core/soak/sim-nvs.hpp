#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "device/drivers/driver-interface.hpp"

namespace soak {

// One device's flash. It outlives the device, so a reboot reopens what was
// committed. Each driver call is one nvs_set + nvs_commit on the ESP32
// (esp32-s3-prefs-driver.hpp).
class SimNvs {
public:
    /** Stores one value with its type tag. */
    void put(const std::string& ns, const std::string& key, char type, const std::string& bytes) {
        values[{ns, key}] = std::string(1, type) + bytes;
    }

    /** The bytes under (ns, key) if stored with @p type. */
    bool get(const std::string& ns, const std::string& key, char type, std::string& out) const {
        std::map<std::pair<std::string, std::string>, std::string>::const_iterator it =
        values.find({ns, key});
        if (it == values.end() || it->second.empty() || it->second[0] != type) return false;
        out = it->second.substr(1);
        return true;
    }

    /** A uint32 value, or @p fallback. */
    uint32_t getUInt(const std::string& ns, const std::string& key, uint32_t fallback) const {
        std::string bytes;
        if (!get(ns, key, 'u', bytes) || bytes.size() != sizeof(uint32_t)) return fallback;
        uint32_t value = 0;
        memcpy(&value, bytes.data(), sizeof(value));
        return value;
    }

private:
    std::map<std::pair<std::string, std::string>, std::string> values;
};

// StorageInterface over a SimNvs. Like the ESP32 driver, it serves only the
// namespaces it was opened with; any other is a harness bug and is counted.
class SimNvsDriver : public StorageDriverInterface {
public:
    /** Opens @p namespaces of @p nvs. */
    SimNvsDriver(const std::string& name, SimNvs& nvs, std::set<std::string> namespaces)
        : StorageDriverInterface(name)
        , nvs(nvs)
        , namespaces(std::move(namespaces)) {}

    /** Nothing to open. */
    int initialize() override { return 0; }
    /** No periodic work. */
    void exec() override {}
    /** Nothing to close. */
    void end() override {}

    /** Stores a string. */
    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        put(ns, key, 's', value);
        return value.size();
    }

    /** Reads a string or @p defaultValue. */
    std::string read(const std::string& ns, const std::string& key,
                     const std::string& defaultValue) override {
        std::string out;
        return opened(ns) && nvs.get(ns, key, 's', out) ? out : defaultValue;
    }

    /** Reads a byte or @p defaultValue. */
    uint8_t readUChar(const std::string& ns, const std::string& key,
                      uint8_t defaultValue) override {
        std::string out;
        return opened(ns) && nvs.get(ns, key, 'c', out) && out.size() == 1
               ? static_cast<uint8_t>(out[0])
               : defaultValue;
    }

    /** Stores a byte. */
    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        put(ns, key, 'c', std::string(1, static_cast<char>(value)));
        return 1;
    }

    /** Reads a uint32 or @p defaultValue. */
    uint32_t readUInt(const std::string& ns, const std::string& key,
                      uint32_t defaultValue) override {
        return opened(ns) ? nvs.getUInt(ns, key, defaultValue) : defaultValue;
    }

    /** Stores a uint32. */
    size_t writeUInt(const std::string& ns, const std::string& key, uint32_t value) override {
        put(ns, key, 'u', std::string(reinterpret_cast<const char*>(&value), sizeof(value)));
        return sizeof(value);
    }

    /** Stores a blob. */
    size_t writeBytes(const std::string& ns, const std::string& key, const uint8_t* data,
                      size_t length) override {
        put(ns, key, 'b', std::string(reinterpret_cast<const char*>(data), length));
        return length;
    }

    /** Reads a blob that fits @p maxLength; 0 when unset or larger. */
    size_t readBytes(const std::string& ns, const std::string& key, uint8_t* buffer,
                     size_t maxLength) override {
        std::string out;
        if (!opened(ns) || !nvs.get(ns, key, 'b', out) || out.size() > maxLength) return 0;
        memcpy(buffer, out.data(), out.size());
        return out.size();
    }

    // Calls on a namespace this driver was not opened with.
    static inline uint64_t unopenedNamespaceCalls = 0;

private:
    bool opened(const std::string& ns) const {
        if (namespaces.count(ns) != 0) return true;
        ++unopenedNamespaceCalls;
        return false;
    }

    void put(const std::string& ns, const std::string& key, char type, const std::string& bytes) {
        if (opened(ns)) nvs.put(ns, key, type, bytes);
    }

    SimNvs& nvs;
    std::set<std::string> namespaces;
};

}  // namespace soak
