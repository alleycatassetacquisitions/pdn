#pragma once

#include <cstddef>

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

/** In-memory test report; encoded by DeviceTestCodec for POST /device-test-results. */
struct DeviceTestResult {
    int hardwareVersion = 1;
    char firmwareCommitHash[9] = "unknown";
    int idLabel = 0;
    char macAddress[18] = "";
    DeviceTestComponents components;
};
