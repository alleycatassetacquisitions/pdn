#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include <cstdint>
#include "cli/cli-device-types.hpp"

namespace cli {

/**
 * Shared utility functions for command handlers.
 */
class CommandUtils {
public:
    /**
     * Find device by ID or index string.
     */
    static int findDevice(const std::string& target,
                          const std::vector<DeviceInstance>& devices,
                          int defaultDevice);

    /**
     * Tokenize a command string by spaces.
     */
    static std::vector<std::string> tokenize(const std::string& cmd);

    /**
     * Parse a MAC address string like "02:00:00:00:00:01" into bytes.
     */
    static void parseMacString(const std::string& macStr, uint8_t* macOut);

    /**
     * Parse hex data from tokens starting at the given index.
     */
    static std::vector<uint8_t> parseHexData(const std::vector<std::string>& tokens, size_t startIndex);
};

} // namespace cli

#endif // NATIVE_BUILD
