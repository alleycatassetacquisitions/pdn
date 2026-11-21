#pragma once

#include <cstdio>
#include <string>

#include "device/wireless-manager.hpp"

/** Stable fabric device id from STA MAC (lowercase hex, no separators). */
inline std::string macAddressDeviceId(WirelessManager* wirelessManager) {
    if (wirelessManager == nullptr) {
        return "unknown";
    }
    uint8_t* mac = wirelessManager->getMacAddress();
    if (mac == nullptr) {
        return "unknown";
    }
    char buf[13] = {};
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5]);
    return std::string(buf);
}
