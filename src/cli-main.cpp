#ifdef NATIVE_BUILD

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

// Platform abstraction
#include "utils/simple-timer.hpp"
#include "id-generator.hpp"

// CLI modules
#include "cli/cli-terminal.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-commands.hpp"

// Native drivers for global instances
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

// Constants
static constexpr int MIN_DEVICES = 1;
static constexpr int MAX_DEVICES = 8;

// Global running flag for signal handling
std::atomic<bool> g_running{true};

// Command input state
std::string g_commandBuffer;
std::string g_commandResult;
int g_selectedDevice = 0;  // Currently selected device index

void signalHandler(int signal) {
    (void)signal;  // Suppress unused parameter warning
    g_running = false;
}

/**
 * Parse command line arguments for device count.
 * Returns -1 if no valid count specified (prompt needed).
 */
int parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        // Check for -n or --count flag
        if ((arg == "-n" || arg == "--count") && i + 1 < argc) {
            int count = std::atoi(argv[i + 1]);
            if (count >= MIN_DEVICES && count <= MAX_DEVICES) {
                return count;
            }
        }
        
        // Check for bare number argument
        int count = std::atoi(arg.c_str());
        if (count >= MIN_DEVICES && count <= MAX_DEVICES) {
            return count;
        }
        
        // Help flag
        if (arg == "-h" || arg == "--help") {
            printf("PDN CLI Simulator\n");
            printf("Usage: %s [options] [device_count]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -n, --count N   Create N devices (1-%d)\n", MAX_DEVICES);
            printf("  -h, --help      Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s           Interactive prompt for device count\n", argv[0]);
            printf("  %s 3         Create 3 devices\n", argv[0]);
            printf("  %s -n 4      Create 4 devices\n", argv[0]);
            exit(0);
        }
    }
    return -1;  // No valid count found, need to prompt
}

/**
 * Prompt user for device count with validation.
 */
int promptDeviceCount() {
    printf("\n");
    printf("\033[1;33m");  // Bold yellow
    printf("How many PDN devices would you like to simulate? (1-%d)\n", MAX_DEVICES);
    printf("\033[0m");
    printf("  - Device 1 will be a Hunter\n");
    printf("  - Device 2 will be a Bounty\n");
    printf("  - Roles alternate: Hunter, Bounty, Hunter, Bounty...\n");
    printf("\n");
    
    while (true) {
        printf("\033[1;36m");  // Bold cyan
        printf("Enter device count [1]: ");
        printf("\033[0m");
        fflush(stdout);
        
        std::string input;
        std::getline(std::cin, input);
        
        // Default to 1 if empty
        if (input.empty()) {
            return 1;
        }
        
        // Parse input
        int count = std::atoi(input.c_str());
        if (count >= MIN_DEVICES && count <= MAX_DEVICES) {
            return count;
        }
        
        printf("\033[1;31m");  // Bold red
        printf("Invalid input. Please enter a number between %d and %d.\n", MIN_DEVICES, MAX_DEVICES);
        printf("\033[0m");
    }
}

/**
 * Create devices with alternating hunter/bounty roles.
 */
std::vector<cli::DeviceInstance> createDevices(int count) {
    std::vector<cli::DeviceInstance> devices;
    devices.reserve(count);
    
    printf("\n\033[1;32m");  // Bold green
    printf("Creating %d device%s...\n", count, count == 1 ? "" : "s");
    printf("\033[0m");
    
    for (int i = 0; i < count; i++) {
        // Alternate roles: even indices are hunters, odd are bounties
        bool isHunter = (i % 2 == 0);
        devices.push_back(cli::DeviceFactory::createDevice(i, isHunter));
        
        printf("  Device %s: %s\n", 
               devices.back().deviceId.c_str(),
               isHunter ? "Hunter" : "Bounty");
    }
    
    printf("\n");
    printf("Press any key to start simulation...\n");
    fflush(stdout);
    
    // Wait for keypress (blocking read)
    #ifndef _WIN32
    getchar();
    #else
    _getch();
    #endif
    
    return devices;
}

int main(int argc, char** argv) {
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create global clock and logger
    NativeClockDriver* globalClock = new NativeClockDriver("global_clock");
    NativeLoggerDriver* globalLogger = new NativeLoggerDriver("global_logger");
    
    // Suppress logger output during prompt phase
    globalLogger->setSuppressOutput(true);
    
    // Set up global platform abstractions
    g_logger = globalLogger;
    SimpleTimer::setPlatformClock(globalClock);
    
    // Seed the ID generator
    IdGenerator(globalClock->milliseconds()).seed(globalClock->milliseconds());
    
    // Show header (before raw mode)
    cli::Terminal::clearScreen();
    cli::printHeader();
    
    // Determine device count from args or prompt
    int deviceCount = parseArgs(argc, argv);
    if (deviceCount < 0) {
        deviceCount = promptDeviceCount();
    }
    
    // Create devices
    std::vector<cli::DeviceInstance> devices = createDevices(deviceCount);
    
    // Now configure terminal for non-blocking input (Unix only)
    #ifndef _WIN32
    struct termios oldTermios = cli::Terminal::enableRawMode();
    #endif
    
    // Set up display for simulation
    cli::Terminal::clearScreen();
    cli::Terminal::hideCursor();
    cli::printHeader();
    
    // Create CLI components
    cli::Renderer renderer;
    cli::CommandProcessor commandProcessor;
    
    // Show initial help hint
    g_commandResult = "Ready! Use LEFT/RIGHT to select device, UP/DOWN for buttons. Type 'help' for commands.";
    
    // Main loop
    while (g_running) {
        // Handle input (non-blocking)
        int key = cli::Terminal::readKey();
        while (key != static_cast<int>(cli::Key::NONE)) {
            if (key == static_cast<int>(cli::Key::ARROW_UP)) {
                // Up arrow = primary button click on selected device
                if (g_selectedDevice >= 0 && g_selectedDevice < static_cast<int>(devices.size())) {
                    devices[g_selectedDevice].primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "Button1 click on " + devices[g_selectedDevice].deviceId;
                }
            } else if (key == static_cast<int>(cli::Key::ARROW_DOWN)) {
                // Down arrow = secondary button click on selected device
                if (g_selectedDevice >= 0 && g_selectedDevice < static_cast<int>(devices.size())) {
                    devices[g_selectedDevice].secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "Button2 click on " + devices[g_selectedDevice].deviceId;
                }
            } else if (key == static_cast<int>(cli::Key::ARROW_LEFT)) {
                // Left arrow = select previous device
                if (g_selectedDevice > 0) {
                    g_selectedDevice--;
                    g_commandResult = "Selected device " + devices[g_selectedDevice].deviceId;
                }
            } else if (key == static_cast<int>(cli::Key::ARROW_RIGHT)) {
                // Right arrow = select next device
                if (g_selectedDevice < static_cast<int>(devices.size()) - 1) {
                    g_selectedDevice++;
                    g_commandResult = "Selected device " + devices[g_selectedDevice].deviceId;
                }
            } else if (key == '\n' || key == '\r') {
                // Execute command on Enter
                auto result = commandProcessor.execute(g_commandBuffer, devices, g_selectedDevice, renderer);
                g_commandResult = result.message;
                if (result.shouldQuit) {
                    g_running = false;
                }
                g_commandBuffer.clear();
            } else if (key == 127 || key == '\b') {
                // Backspace - remove last character
                if (!g_commandBuffer.empty()) {
                    g_commandBuffer.pop_back();
                }
            } else if (key == 27) {
                // Escape - clear command buffer
                g_commandBuffer.clear();
                g_commandResult.clear();
            } else if (key == 3) {
                // Ctrl+C - quit
                g_running = false;
            } else if (key >= 32 && key < 127) {
                // Printable character - add to buffer
                g_commandBuffer += static_cast<char>(key);
            }
            key = cli::Terminal::readKey();
        }
        
        // Deliver pending peer-to-peer messages
        NativePeerBroker::getInstance().deliverPackets();
        
        // Transfer serial data between connected devices
        cli::SerialCableBroker::getInstance().transferData();
        
        // Update all devices
        for (auto& device : devices) {
            device.pdn->loop();
        }
        
        // Render UI
        renderer.renderUI(devices, g_commandResult, g_commandBuffer, g_selectedDevice);
        
        // Sleep to maintain ~30 FPS update rate
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    
    // Restore terminal settings and cursor
    cli::Terminal::showCursor();
    
    // Move cursor below the UI for clean shutdown message
    printf("\n\nShutting down...\n");
    
    for (auto& device : devices) {
        cli::DeviceFactory::destroyDevice(device);
    }
    
    delete globalLogger;
    delete globalClock;
    
    return 0;
}

#endif // NATIVE_BUILD
