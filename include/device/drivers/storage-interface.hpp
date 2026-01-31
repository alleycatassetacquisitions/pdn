#pragma once

#include <string>

class StorageInterface {
public:
    virtual ~StorageInterface() = default;
    virtual size_t write(const std::string& key, const std::string& value) = 0;
    virtual std::string read(const std::string& key, const std::string& defaultValue) = 0;
    virtual bool remove(const std::string& key) = 0;
    virtual bool clear() = 0;
    virtual void end() = 0;
    virtual uint8_t readUChar(const std::string& key, uint8_t defaultValue) = 0;
    virtual size_t writeUChar(const std::string& key, uint8_t value) = 0;
};