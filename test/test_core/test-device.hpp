#pragma once

#include <string>

// Native drivers
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-display-driver.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/drivers/native/native-haptics-driver.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"

// Core
#include "device/pdn.hpp"
#include "utils/simple-timer.hpp"

/*
 * TestDevice: RAII wrapper around PDN with all 12 native drivers.
 *
 * Unlike MockDevice (which uses GMock interfaces), TestDevice uses real native
 * drivers — the same ones the CLI simulator uses. This gives tests access to
 * actual driver behavior (buffer limits, pixel state, callback dispatch) instead
 * of mocked expectations.
 *
 * TestDevice coexists with MockDevice. Existing tests continue to use MockDevice;
 * new tests (starting with contract tests) use TestDevice.
 *
 * Usage:
 *   TestDevice td;
 *   td.pdn->begin();
 *   td.displayDriver->getPixel(0, 0);
 *   td.pressButton();
 */
class TestDevice {
public:
    // The PDN instance — pass to anything needing Device*
    PDN* pdn;

    // Typed driver pointers (non-owning — PDN owns via DriverManager)
    NativeDisplayDriver* displayDriver;
    NativeButtonDriver* primaryButtonDriver;
    NativeButtonDriver* secondaryButtonDriver;
    NativeHapticsDriver* hapticsDriver;
    NativeClockDriver* clockDriver;
    NativeSerialDriver* serialOutDriver;
    NativeSerialDriver* serialInDriver;
    NativeLightStripDriver* lightDriver;
    NativeHttpClientDriver* httpClientDriver;
    NativePeerCommsDriver* peerCommsDriver;
    NativePrefsDriver* storageDriver;
    NativeLoggerDriver* loggerDriver;

    TestDevice() {
        // Allocate all 12 native drivers
        loggerDriver = new NativeLoggerDriver(LOGGER_DRIVER_NAME);
        loggerDriver->setSuppressOutput(true);
        clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME);
        displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME);
        primaryButtonDriver = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME, 0);
        secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME, 1);
        lightDriver = new NativeLightStripDriver(LIGHT_DRIVER_NAME);
        hapticsDriver = new NativeHapticsDriver(HAPTICS_DRIVER_NAME, 0);
        serialOutDriver = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME);
        serialInDriver = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME);
        httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME);
        peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME);
        storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME);

        // Build DriverConfig map
        DriverConfig config = {
            {DISPLAY_DRIVER_NAME, displayDriver},
            {PRIMARY_BUTTON_DRIVER_NAME, primaryButtonDriver},
            {SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonDriver},
            {LIGHT_DRIVER_NAME, lightDriver},
            {HAPTICS_DRIVER_NAME, hapticsDriver},
            {SERIAL_OUT_DRIVER_NAME, serialOutDriver},
            {SERIAL_IN_DRIVER_NAME, serialInDriver},
            {HTTP_CLIENT_DRIVER_NAME, httpClientDriver},
            {PEER_COMMS_DRIVER_NAME, peerCommsDriver},
            {PLATFORM_CLOCK_DRIVER_NAME, clockDriver},
            {LOGGER_DRIVER_NAME, loggerDriver},
            {STORAGE_DRIVER_NAME, storageDriver},
        };

        // Create PDN (takes ownership of drivers via DriverManager)
        pdn = PDN::createPDN(config);

        // Set platform clock for SimpleTimer
        SimpleTimer::setPlatformClock(clockDriver);
    }

    ~TestDevice() {
        // Reset global clock before destroying PDN
        SimpleTimer::resetClock();

        // delete pdn cascades: PDN::~PDN() → shutdownApps() → ~Device() → dismountDrivers()
        // which deletes all drivers. After this, all driver pointers are dangling.
        delete pdn;
    }

    // Simulate a button press on the primary button
    void pressButton(ButtonInteraction interaction = ButtonInteraction::PRESS) {
        primaryButtonDriver->execCallback(interaction);
    }

    // Inject a serial message into the output jack (as if received from cable)
    void simulateSerialReceive(const std::string& message) {
        serialOutDriver->injectInput(message);
    }

    // Advance the clock by the given number of milliseconds
    void advanceTime(unsigned long ms) {
        clockDriver->advance(ms);
    }
};
