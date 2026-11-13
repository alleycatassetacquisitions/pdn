#pragma once

#include "device/drivers/logger.hpp"
#include <cstring>
#include "device/drivers/peer-comms-interface.hpp"

// Utility functions
inline uint64_t MacToUInt64(const uint8_t* macAddr)
{
    uint64_t tmp = 0;
    for(int i = 0; i < 6; ++i)
        tmp = (tmp << 8) + macAddr[i];

    return tmp;
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