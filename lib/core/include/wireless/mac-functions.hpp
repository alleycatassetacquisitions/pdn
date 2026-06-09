#pragma once

#include "device/drivers/logger.hpp"
#include <cstring>
#include <array>
#include <vector>
#include "device/drivers/peer-comms-interface.hpp"

// Utility functions
inline uint64_t MacToUInt64(const uint8_t* macAddr)
{
    uint64_t tmp = 0;
    for(int i = 0; i < 6; ++i)
        tmp = (tmp << 8) + macAddr[i];

    return tmp;
}

inline bool macInList(const uint8_t* mac, const std::vector<std::array<uint8_t, 6>>& list)
{
    for (const auto& m : list)
        if (std::memcmp(m.data(), mac, 6) == 0) return true;
    return false;
}

// Lowest MAC (lexicographic by byte) in a set; the lowest-MAC member is the
// coordinator. An empty set yields the broadcast MAC (all 0xFF), a sentinel
// above any real address.
inline std::array<uint8_t, 6> lowestMacIn(const std::vector<std::array<uint8_t, 6>>& list)
{
    std::array<uint8_t, 6> lowest = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (const auto& m : list)
        if (std::memcmp(m.data(), lowest.data(), 6) < 0) lowest = m;
    return lowest;
}

inline const char* MacToString(const uint8_t* macAddr)
{
    static char macStr[18];
    snprintf(macStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
        macAddr[0], macAddr[1], macAddr[2],
        macAddr[3], macAddr[4], macAddr[5]);

    macStr[17] = '\0';
    return macStr;
}

inline bool StringToMac(const char* macStr, uint8_t* macAddr)
{
    LOG_I("ENC", "Converting MAC string '%s' to bytes", macStr);

    if(strlen(macStr) != 17) { // XX:XX:XX:XX:XX:XX
        LOG_E("ENC", "Invalid MAC string length: %d", strlen(macStr));
        return false;
    }

    unsigned int values[6];
    int result = sscanf(macStr, "%X:%X:%X:%X:%X:%X",
        &values[0], &values[1], &values[2],
        &values[3], &values[4], &values[5]);

    if(result != 6) {
        LOG_E("ENC", "Failed to parse MAC string, only got %d values", result);
        return false;
    }

    for(int i = 0; i < 6; i++)
    {
        if(values[i] > 0xFF) {
            LOG_E("ENC", "MAC byte %d value 0x%X exceeds 0xFF", i, values[i]);
            return false;
        }
        macAddr[i] = (uint8_t)values[i];
    }

    LOG_I("ENC", "Successfully converted MAC string to bytes");
    return true;
}