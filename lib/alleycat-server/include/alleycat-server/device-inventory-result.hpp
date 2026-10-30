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
    /** Permanent device UUID when known; otherwise the codec derives a stable id from @p
     * macAddress. */
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
