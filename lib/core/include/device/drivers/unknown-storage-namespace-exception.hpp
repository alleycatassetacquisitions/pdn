#pragma once

#include <stdexcept>
#include <string>

/** Thrown when StorageInterface methods use an NVS namespace not registered on the driver. */
class UnknownStorageNamespaceException : public std::runtime_error {
public:
    explicit UnknownStorageNamespaceException(const std::string& namespaceName)
        : std::runtime_error("Storage namespace not registered: \"" + namespaceName + "\"")
        , namespaceName(namespaceName) {}

    const std::string& getNamespaceName() const { return namespaceName; }

private:
    std::string namespaceName;
};
