#pragma once

#include <string>

class StorageInterface {
public:
    virtual ~StorageInterface() = default;
    virtual size_t write(const std::string& ns, const std::string& key, const std::string& value) = 0;
    virtual std::string read(const std::string& ns, const std::string& key, const std::string& defaultValue) = 0;
    virtual bool remove(const std::string& ns, const std::string& key) = 0;
    virtual bool clear(const std::string& ns) = 0;
    virtual void end() = 0;
    virtual uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) = 0;
    virtual size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) = 0;
    /** Byte-exact write (embedded 0x00 survives, unlike write()). Returns bytes written, 0 on failure. */
    virtual size_t writeBlob(const std::string& ns, const std::string& key, const std::string& data) = 0;
    /** Byte-exact read; empty when the key is missing or unreadable. */
    virtual std::string readBlob(const std::string& ns, const std::string& key) = 0;
};
