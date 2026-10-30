#pragma once

#include <cstddef>
#include <cstdint>

#include "utils/uuid-bytes.hpp"

struct DeviceTestComponents {
    bool screen = false;
    bool leds = false;
    bool primaryButton = false;
    bool secondaryButton = false;
    bool haptics = false;
    bool powerSwitch = false;
    bool inputJack = false;
    bool outputJack = false;
    bool housing = false;
};

/** Bench session report payload for POST /device-reports (DeviceInventoryCodec). */
struct DeviceTestResult {
    /** Device UUID when known; codec derives a stable one from macAddress otherwise. */
    UUIDBytes deviceId;
    /** One UUID per bench check; retain for upload retries. */
    UUIDBytes inspectionId;
    int64_t inspectedAtUnixMs = 0;
    int hardwareVersion = 1;
    char firmwareCommitHash[9] = "unknown";
    int idLabel = 0;
    char macAddress[18] = "";
    /** Pre-jack power-on check: true = powered on, false = failed to power on. */
    bool powersOn = false;
    DeviceTestComponents components;
};
