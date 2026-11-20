#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "device/drivers/driver-interface.hpp"

namespace soak {

// One device's flash. It outlives the device, so a reboot or power cut reopens
// what was committed. Each driver call is one nvs_set + nvs_commit on the ESP32
// (esp32-s3-prefs-driver.hpp), so one call is the unit a power cut keeps or loses.
class SimNvs {
public:
    // NVS stores a blob in 32-byte entries; a torn blob keeps whole entries only.
    static constexpr size_t WRITE_GRANULE = 32;

    /** Arms a power cut at the @p k-th write from now (1 = the next one); 0 disarms. */
    void armCut(uint32_t k, bool torn) {
        cutCountdown = k;
        cutTorn = torn;
        if (k > 0) ++cutsArmed;
    }

    /** True once the armed cut fired: later writes are dropped. */
    bool isFrozen() const { return frozen; }

    /** Power returns: writes stick again. */
    void thaw() {
        frozen = false;
        cutCountdown = 0;
    }

    /**
     * Stores one value with its type tag, unless a power cut swallows it. The driver
     * still reports success: the firmware is dead and its control flow must not
     * change in the tick it dies in.
     */
    void put(const std::string& ns, const std::string& key, char type, const std::string& bytes) {
        if (frozen) return;
        if (cutCountdown > 0 && --cutCountdown == 0) {
            frozen = true;
            ++cutsFired;
            if (onCut) onCut(ns, key);
            const size_t kept = (bytes.size() / 2 / WRITE_GRANULE) * WRITE_GRANULE;
            if (cutTorn && type == 'b' && kept != 0)
                values[{ns, key}] = std::string(1, type) + bytes.substr(0, kept);
            return;
        }
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

    // Fires the moment the armed write dies, with the write it killed, so the world can
    // silence the radio and tell what was lost.
    std::function<void(const std::string& ns, const std::string& key)> onCut;
    // Cuts ever armed and ever fired; an armed cut is dropped by a later arm, a disarm
    // or a boot, so the difference is the cuts that never happened.
    uint64_t cutsArmed = 0;
    uint64_t cutsFired = 0;

private:
    std::map<std::pair<std::string, std::string>, std::string> values;
    uint32_t cutCountdown = 0;
    bool cutTorn = false;
    bool frozen = false;
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
