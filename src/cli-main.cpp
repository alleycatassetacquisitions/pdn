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

// CLI modules
#include "cli/cli-terminal.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-commands.hpp"

// Native drivers for global instances
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

// Global running flag for signal handling
std::atomic<bool> g_running{true};

// Command input state
std::string g_commandBuffer;
std::string g_commandResult;
int g_selectedDevice = 0;  // Currently selected device index

void signalHandler(int signal) {
    g_running = false;
}

int main(int argc, char** argv) {
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create global clock and logger
    NativeClockDriver* globalClock = new NativeClockDriver("global_clock");
    NativeLoggerDriver* globalLogger = new NativeLoggerDriver("global_logger");
    
    // Suppress logger output - we'll display logs in the UI later
    globalLogger->setSuppressOutput(true);
    
    // Set up global platform abstractions
    g_logger = globalLogger;
    SimpleTimer::setPlatformClock(globalClock);
    
    // Seed the ID generator
    IdGenerator(globalClock->milliseconds()).seed(globalClock->milliseconds());
    
    // Configure terminal for non-blocking input (Unix only)
    #ifndef _WIN32
    struct termios oldTermios = cli::Terminal::enableRawMode();
    #endif
    
    // Set up display
    cli::Terminal::clearScreen();
    cli::Terminal::hideCursor();
    cli::printHeader();
    
    // Create CLI components
    cli::Renderer renderer;
    cli::CommandProcessor commandProcessor;
    
    // Create a single hunter device with ID 0010
    std::vector<cli::DeviceInstance> devices;
    devices.push_back(cli::DeviceFactory::createDevice(0, true));  // Device 0, Hunter
    
    // Main loop
    while (g_running) {
        // Handle input (non-blocking)
        #ifndef _WIN32
        int key = cli::Terminal::readKey();
        while (key != static_cast<int>(cli::Key::NONE)) {
            if (key == static_cast<int>(cli::Key::ARROW_UP)) {
                // Up arrow = primary button click on selected device
                if (g_selectedDevice >= 0 && g_selectedDevice < static_cast<int>(devices.size())) {
                    devices[g_selectedDevice].primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "Button1 click (UP) on " + devices[g_selectedDevice].deviceId;
                }
            } else if (key == static_cast<int>(cli::Key::ARROW_DOWN)) {
                // Down arrow = secondary button click on selected device
                if (g_selectedDevice >= 0 && g_selectedDevice < static_cast<int>(devices.size())) {
                    devices[g_selectedDevice].secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "Button2 click (DOWN) on " + devices[g_selectedDevice].deviceId;
                }
            } else if (key == '\n' || key == '\r') {
                // Execute command on Enter
                auto result = commandProcessor.execute(g_commandBuffer, devices, g_selectedDevice);
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
        #endif
        
        // Deliver pending peer-to-peer messages
        NativePeerBroker::getInstance().deliverPackets();
        
        // Update all devices
        for (auto& device : devices) {
            device.pdn->loop();
            device.game->loop();
        }
        
        // Render UI
        renderer.renderUI(devices, g_commandResult, g_commandBuffer, g_selectedDevice);
        
        // Sleep to maintain ~30 FPS update rate
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    
    // Restore terminal settings and cursor
    cli::Terminal::showCursor();
    #ifndef _WIN32
    cli::Terminal::restoreTerminal(oldTermios);
    #endif
    
    // Move cursor below the UI for clean shutdown message
    printf("\nShutting down...\n");
    
    for (auto& device : devices) {
        cli::DeviceFactory::destroyDevice(device);
    }
    
    delete globalLogger;
    delete globalClock;
    
    return 0;
}

#endif // NATIVE_BUILD
