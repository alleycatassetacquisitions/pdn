#ifdef NATIVE_BUILD

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

// Platform abstraction
#include "utils/simple-timer.hpp"
#include "id-generator.hpp"

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
#include "device/drivers/native/native-peer-broker.hpp"
#include "device/drivers/native/native-dashboard.hpp"

// Core
#include "device/pdn.hpp"
#include "game/player.hpp"
#include "game/quickdraw.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"

// Global running flag for signal handling
std::atomic<bool> running{true};

void signalHandler(int signal) {
    running = false;
}

// Structure to hold all drivers for a single device instance
struct DeviceInstance {
    // Drivers (owned by DriverManager via PDN, but we keep pointers for dashboard access)
    NativeClockDriver* clockDriver;
    NativeLoggerDriver* loggerDriver;
    NativeDisplayDriver* displayDriver;
    NativeButtonDriver* primaryButtonDriver;
    NativeButtonDriver* secondaryButtonDriver;
    NativeLightStripDriver* lightDriver;
    NativeHapticsDriver* hapticsDriver;
    NativeSerialDriver* serialOutDriver;
    NativeSerialDriver* serialInDriver;
    NativeHttpClientDriver* httpClientDriver;
    NativePeerCommsDriver* peerCommsDriver;
    NativePrefsDriver* storageDriver;
    
    // Game objects
    PDN* pdn;
    Player* player;
    Quickdraw* game;
    QuickdrawWirelessManager* wirelessManager;
};

DeviceInstance createDeviceInstance(int deviceIndex) {
    DeviceInstance instance;
    
    // Create all drivers
    std::string suffix = "_" + std::to_string(deviceIndex);
    
    instance.loggerDriver = new NativeLoggerDriver(LOGGER_DRIVER_NAME + suffix);
    instance.clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME + suffix);
    instance.displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
    instance.primaryButtonDriver = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
    instance.secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
    instance.lightDriver = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
    instance.hapticsDriver = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
    instance.serialOutDriver = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
    instance.serialInDriver = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
    instance.httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
    instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
    instance.storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);
    
    // Create driver configuration
    DriverConfig pdnConfig = {
        {DISPLAY_DRIVER_NAME, instance.displayDriver},
        {PRIMARY_BUTTON_DRIVER_NAME, instance.primaryButtonDriver},
        {SECONDARY_BUTTON_DRIVER_NAME, instance.secondaryButtonDriver},
        {LIGHT_DRIVER_NAME, instance.lightDriver},
        {HAPTICS_DRIVER_NAME, instance.hapticsDriver},
        {SERIAL_OUT_DRIVER_NAME, instance.serialOutDriver},
        {SERIAL_IN_DRIVER_NAME, instance.serialInDriver},
        {HTTP_CLIENT_DRIVER_NAME, instance.httpClientDriver},
        {PEER_COMMS_DRIVER_NAME, instance.peerCommsDriver},
        {PLATFORM_CLOCK_DRIVER_NAME, instance.clockDriver},
        {LOGGER_DRIVER_NAME, instance.loggerDriver},
        {STORAGE_DRIVER_NAME, instance.storageDriver},
    };
    
    // Create PDN and game
    instance.pdn = PDN::createPDN(pdnConfig);
    instance.player = new Player();
    instance.player->setUserID(IdGenerator(instance.clockDriver->milliseconds()).generateId());
    instance.pdn->begin();
    
    // Create wireless manager for this device
    // Note: QuickdrawWirelessManager is a singleton, so we skip it for now
    // In a full implementation, we'd need per-device wireless managers
    instance.wirelessManager = nullptr;
    
    // Create game
    instance.game = new Quickdraw(instance.player, instance.pdn, instance.wirelessManager, nullptr);

    // Register state machines with the device and launch Quickdraw
    AppConfig apps = {
        {StateId(QUICKDRAW_APP_ID), instance.game}
    };
    instance.pdn->loadAppConfig(apps, StateId(QUICKDRAW_APP_ID));
    
    return instance;
}

void printUsage(const char* programName) {
    printf("Usage: %s [OPTIONS]\n", programName);
    printf("Options:\n");
    printf("  -n NUM    Number of devices to spawn (default: 2)\n");
    printf("  -h        Show this help message\n");
}

int main(int argc, char** argv) {
    int numDevices = 2;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            numDevices = atoi(argv[++i]);
            if (numDevices < 1) numDevices = 1;
            if (numDevices > 8) numDevices = 8;  // Reasonable limit
        } else if (strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    printf("Starting PDN Native Simulator with %d device(s)...\n", numDevices);
    
    // Create the first device's clock and logger for global use
    NativeClockDriver* globalClock = new NativeClockDriver("global_clock");
    NativeLoggerDriver* globalLogger = new NativeLoggerDriver("global_logger");
    
    // Set up global platform abstractions
    g_logger = globalLogger;
    SimpleTimer::setPlatformClock(globalClock);
    
    // Seed the ID generator
    IdGenerator(globalClock->milliseconds()).seed(globalClock->milliseconds());
    
    // Create device instances
    std::vector<DeviceInstance> devices;
    std::vector<PDN*> pdnPtrs;
    std::vector<Quickdraw*> gamePtrs;
    std::vector<NativeLightStripDriver*> lightPtrs;
    std::vector<NativePeerCommsDriver*> peerPtrs;
    
    for (int i = 0; i < numDevices; i++) {
        devices.push_back(createDeviceInstance(i));
        pdnPtrs.push_back(devices[i].pdn);
        gamePtrs.push_back(devices[i].game);
        lightPtrs.push_back(devices[i].lightDriver);
        peerPtrs.push_back(devices[i].peerCommsDriver);
    }
    
    // Create dashboard
    NativeDashboard dashboard(numDevices);
    dashboard.initialize();
    
    // Selected device for input commands
    int selectedDevice = 0;
    
    // Configure terminal for non-blocking input
    #ifndef _WIN32
    // Unix: use termios for non-blocking input
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
    struct termios oldTermios, newTermios;
    tcgetattr(STDIN_FILENO, &oldTermios);
    newTermios = oldTermios;
    newTermios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    #endif
    
    // Main loop
    while (running) {
        // Handle input (non-blocking)
        #ifndef _WIN32
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == 'q' || c == 'Q') {
                running = false;
            } else if (c >= '1' && c <= '0' + numDevices) {
                selectedDevice = c - '1';
            } else if (c == 'b' || c == 'B') {
                // Simulate button press on selected device
                if (selectedDevice < numDevices) {
                    devices[selectedDevice].primaryButtonDriver->execCallback(ButtonInteraction::PRESS);
                }
            } else if (c == 'l' || c == 'L') {
                // Simulate long press on selected device
                if (selectedDevice < numDevices) {
                    devices[selectedDevice].primaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
                }
            }
        }
        #endif
        
        // Deliver pending peer-to-peer messages
        NativePeerBroker::getInstance().deliverPackets();
        
        // Update all devices
        for (auto& device : devices) {
            device.pdn->loop();
        }
        
        // Render dashboard
        dashboard.render(pdnPtrs, gamePtrs, lightPtrs, peerPtrs);
        
        // Sleep to maintain ~30 FPS update rate
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    
    // Cleanup
    dashboard.cleanup();
    
    #ifndef _WIN32
    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldTermios);
    #endif
    
    printf("Shutting down...\n");
    
    // Clean up devices
    for (auto& device : devices) {
        delete device.game;
        delete device.player;
        delete device.pdn;  // This deletes drivers via DriverManager::dismountDrivers()
        // Note: drivers are deleted by PDN's destructor, so we don't delete them here
    }

    // Clean up global drivers (these are NOT owned by any PDN instance)
    delete globalLogger;
    delete globalClock;

    return 0;
}

#endif // NATIVE_BUILD
