#pragma once

#include <array>
#include <cstdint>

#include "alleycat-server/device-inventory-result.hpp"

struct DeviceTestSession {
    DeviceTestResult result;
    std::array<uint8_t, 6> peerMac = {};
    bool peerMacValid = false;
    bool inputJackTested = false;
    bool remoteChecksEnabled = true;
};
