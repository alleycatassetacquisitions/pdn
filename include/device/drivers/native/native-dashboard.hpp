#pragma once

#include <vector>
#include <string>
#include <cstdio>
#include <chrono>
#include <sstream>
#include <iomanip>

#include "device/device.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-display-driver.hpp"

/**
 * ANSI escape code based dashboard for displaying multiple PDN device states.
 * Renders in-place updates without scrolling output.
 */
class NativeDashboard {
public:
    NativeDashboard(int numDevices) : numDevices_(numDevices) {
        startTime_ = std::chrono::steady_clock::now();
    }

    /**
     * Clear screen and set up initial layout.
     */
    void initialize() {
        // Clear screen
        printf("\033[2J");
        // Hide cursor
        printf("\033[?25l");
        // Move to top-left
        printf("\033[H");
        fflush(stdout);
    }

    /**
     * Cleanup on exit - show cursor, reset colors.
     */
    void cleanup() {
        printf("\033[?25h"); // Show cursor
        printf("\033[0m");   // Reset colors
        printf("\n");
        fflush(stdout);
    }

    /**
     * Render the dashboard with current device states.
     */
    void render(const std::vector<PDN*>& devices, 
                const std::vector<Quickdraw*>& games,
                const std::vector<NativeLightStripDriver*>& lightDrivers,
                const std::vector<NativePeerCommsDriver*>& peerDrivers) {
        
        // Move cursor to top
        printf("\033[H");

        // Calculate runtime
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        int hours = elapsed / 3600;
        int mins = (elapsed % 3600) / 60;
        int secs = elapsed % 60;

        // Header
        printf("\033[1;36m"); // Bold cyan
        printf("╔══════════════════════════════════════════════════════════════════════════════╗\n");
        printf("║  PDN Native Simulator                                    Runtime: %02d:%02d:%02d  ║\n", 
               hours, mins, secs);
        printf("║  Devices: %d                                                                  ║\n", 
               numDevices_);
        printf("╠══════════════════════════════════════════════════════════════════════════════╣\n");
        printf("\033[0m");

        // Render each device panel
        for (int i = 0; i < numDevices_ && i < (int)devices.size(); i++) {
            renderDevicePanel(i, devices[i], games[i], lightDrivers[i], peerDrivers[i]);
        }

        // Footer with controls
        printf("\033[1;36m");
        printf("╠══════════════════════════════════════════════════════════════════════════════╣\n");
        printf("║  Controls: [1-%d] Select device  [B]utton press  [Q]uit                      ║\n", 
               numDevices_);
        printf("╚══════════════════════════════════════════════════════════════════════════════╝\n");
        printf("\033[0m");

        fflush(stdout);
    }

private:
    void renderDevicePanel(int index, PDN* device, Quickdraw* game,
                          NativeLightStripDriver* lights, 
                          NativePeerCommsDriver* peerComms) {
        
        // Get current state info
        State* currentState = game->getCurrentState();
        int stateId = currentState ? currentState->getStateId() : -1;
        std::string deviceId = device->getDeviceId();
        if (deviceId.empty()) deviceId = "???";
        
        // Device header
        printf("║ \033[1;33mDevice %d\033[0m [%s] MAC: %s\n", 
               index + 1, 
               deviceId.c_str(),
               peerComms->getMacString().c_str());
        
        // State info
        printf("║   State: \033[1;32m%d\033[0m\n", stateId);
        
        // LED visualization
        printf("║   LEDs: ");
        renderLEDStrip(lights);
        printf("\n");
        
        // Separator between devices
        printf("║ ─────────────────────────────────────────────────────────────────────────────\n");
    }

    void renderLEDStrip(NativeLightStripDriver* lights) {
        // Render left lights (9 LEDs)
        printf("L[");
        for (int i = 0; i < 9; i++) {
            auto led = lights->getLeftLights()[i];
            printColoredBlock(led);
        }
        printf("] ");

        // Transmit light
        printf("T[");
        printColoredBlock(lights->getTransmitLight());
        printf("] ");

        // Right lights (9 LEDs)
        printf("R[");
        for (int i = 0; i < 9; i++) {
            auto led = lights->getRightLights()[i];
            printColoredBlock(led);
        }
        printf("]");
    }

    void printColoredBlock(const LEDState::SingleLEDState& led) {
        // Use 24-bit color if terminal supports it
        if (led.brightness > 0 && (led.color.red > 0 || led.color.green > 0 || led.color.blue > 0)) {
            // Scale by brightness
            int r = (led.color.red * led.brightness) / 255;
            int g = (led.color.green * led.brightness) / 255;
            int b = (led.color.blue * led.brightness) / 255;
            printf("\033[48;2;%d;%d;%dm \033[0m", r, g, b);
        } else {
            // Off - dark gray
            printf("\033[48;2;30;30;30m \033[0m");
        }
    }

    int numDevices_;
    std::chrono::steady_clock::time_point startTime_;
};
