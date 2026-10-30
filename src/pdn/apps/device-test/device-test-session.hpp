#pragma once

#include <array>
#include <cstdint>

#include "alleycat-server/device-inventory-result.hpp"
#include "device/drivers/serial-wrapper.hpp"

struct DeviceTestSession {
    DeviceTestResult result;
    std::array<uint8_t, 6> peerMac = {};
    bool peerMacValid = false;
    SerialIdentifier sessionPort = SerialIdentifier::INPUT_JACK;
    bool inputJackTested = false;
    bool remoteChecksEnabled = true;
    bool sessionInfoReceived = false;
};
