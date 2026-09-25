#pragma once

#include <string>

class StorageInterface {
public:
    /// Implementations own their backing store and close it here.
    virtual ~StorageInterface() = default;
    /// Stores a string under `ns`; returns the number of bytes written.
    virtual size_t write(const std::string& ns, const std::string& key, const std::string& value) = 0;
    /// Reads a string back, or `defaultValue` when the key is unset.
    virtual std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) = 0;
    /// Drops one key from `ns`. True when something was removed.
    virtual bool remove(const std::string& ns, const std::string& key) = 0;
    /// Drops every key in `ns`.
    virtual bool clear(const std::string& ns) = 0;
    /// Flushes and closes the backing store.
    virtual void end() = 0;
    /// Reads one byte back, or `defaultValue` when the key is unset.
    virtual uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) = 0;
    /// Stores one byte under `ns`.
    virtual size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) = 0;
};
