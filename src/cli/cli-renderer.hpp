#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <sys/ioctl.h>

#include "cli/cli-terminal.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-diagnostics.hpp"

namespace cli {

/**
 * Renderer for the CLI simulator UI.
 * Uses differential rendering to only update changed lines.
 */
class Renderer {
public:
    // Status display starting row (after 12-line header + 1 blank line)
    static constexpr int STATUS_START_ROW = 14;
    
    Renderer() = default;

    /** Enables or disables the ASCII display mirror. */
    void setDisplayMirror(bool enabled) { displayMirrorEnabled = enabled; }
    /** Toggles the ASCII display mirror. */
    void toggleDisplayMirror() { displayMirrorEnabled = !displayMirrorEnabled; }
    /** Returns whether the ASCII display mirror is enabled. */
    bool isDisplayMirrorEnabled() const { return displayMirrorEnabled; }

    /** Enables or disables display row captions. */
    void setCaptions(bool enabled) { captionsEnabled = enabled; }
    /** Toggles display row captions. */
    void toggleCaptions() { captionsEnabled = !captionsEnabled; }
    /** Returns whether display row captions are enabled. */
    bool isCaptionsEnabled() const { return captionsEnabled; }

    /**
     * Render the full UI for all devices, command result, and prompt.
     */
    void renderUI(std::vector<DeviceInstance>& devices,
                  const std::string& commandResult,
                  const std::string& commandBuffer,
                  int selectedDeviceIndex) {
        currentFrame_.clear();
        
        // Device selector bar (shows all devices, highlights selected)
        if (devices.size() > 1) {
            std::string selectorBar = "Devices: ";
            for (size_t i = 0; i < devices.size(); i++) {
                if (i > 0) selectorBar += "  ";
                
                if (static_cast<int>(i) == selectedDeviceIndex) {
                    // Selected device - highlighted
                    selectorBar += format("\033[1;7;33m[%d] %s %s\033[0m",
                                          static_cast<int>(i),
                                          devices[i].deviceId.c_str(),
                                          devices[i].isHunter ? "H" : "B");
                } else {
                    // Unselected device - dim
                    selectorBar += format("\033[90m[%d] %s %s\033[0m",
                                          static_cast<int>(i),
                                          devices[i].deviceId.c_str(),
                                          devices[i].isHunter ? "H" : "B");
                }
            }
            selectorBar += "   \033[90m(LEFT/RIGHT to switch)\033[0m";
            bufferLine(selectorBar);
            bufferLine("");
        }
        
        // Render only the selected device panel
        if (selectedDeviceIndex >= 0 && selectedDeviceIndex < static_cast<int>(devices.size())) {
            renderDevicePanel(devices[selectedDeviceIndex], true);
        }
        
        // Blank line
        bufferLine("");
        
        // Command result (if any)
        if (!commandResult.empty()) {
            bufferLine(format("\033[1;32m> %s\033[0m", commandResult.c_str()));
        } else {
            bufferLine("");
        }
        
        // Command prompt with cursor indicator
        const char* deviceId = devices.empty() ? "---" : devices[selectedDeviceIndex].deviceId.c_str();
        std::string promptLine = format(
            "\033[1;36m[%s]>\033[0m %s\033[7m \033[0m   \033[90m(type 'help' for commands)\033[0m",
            deviceId,
            commandBuffer.c_str());
        bufferLine(promptLine);
        
        // Render only the changed lines
        renderDiff();
    }

private:
    bool displayMirrorEnabled = false;
    bool captionsEnabled = true;
    std::vector<std::string> previousFrame_;
    std::vector<std::string> currentFrame_;
    
    /**
     * Add a line to the current frame buffer.
     */
    void bufferLine(const std::string& line) {
        currentFrame_.push_back(line);
    }
    
    /**
     * Get terminal height to prevent cursor positioning past the screen edge.
     * Writing past the terminal height causes scrolling that corrupts
     * all subsequent absolute cursor positions.
     */
    static int getTerminalHeight() {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
            return static_cast<int>(ws.ws_row);
        }
        return 50;
    }

    /**
     * Render the current frame using differential updates.
     * Lines beyond terminal height are skipped to prevent scroll corruption.
     */
    void renderDiff() {
        int termHeight = getTerminalHeight();

        // Ensure we have enough lines in previous frame for comparison
        while (previousFrame_.size() < currentFrame_.size()) {
            previousFrame_.push_back("");
        }

        // Compare and update only changed lines (within terminal bounds)
        for (size_t i = 0; i < currentFrame_.size(); i++) {
            int row = STATUS_START_ROW + static_cast<int>(i);
            if (row >= termHeight) break;

            if (i >= previousFrame_.size() || currentFrame_[i] != previousFrame_[i]) {
                Terminal::moveCursor(row, 1);
                Terminal::clearLine();
                printf("%s", currentFrame_[i].c_str());
            }
        }

        // Clear any extra lines from previous frame (within terminal bounds)
        if (previousFrame_.size() > currentFrame_.size()) {
            for (size_t i = currentFrame_.size(); i < previousFrame_.size(); i++) {
                int row = STATUS_START_ROW + static_cast<int>(i);
                if (row >= termHeight) break;
                Terminal::moveCursor(row, 1);
                Terminal::clearLine();
            }
        }

        // Store current frame as previous for next comparison
        previousFrame_ = currentFrame_;
        currentFrame_.clear();

        Terminal::flush();
    }
    
    /**
     * Helper to render LED as colored block.
     */
    static std::string renderLEDStr(const LEDState::SingleLEDState& led, bool showRGB = false) {
        char buf[64];
        if (led.brightness > 0 && (led.color.red > 0 || led.color.green > 0 || led.color.blue > 0)) {
            int r = (led.color.red * led.brightness) / 255;
            int g = (led.color.green * led.brightness) / 255;
            int b = (led.color.blue * led.brightness) / 255;
            if (showRGB) {
                snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm#\033[0m", r, g, b);
            } else {
                snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm \033[0m", r, g, b);
            }
        } else {
            snprintf(buf, sizeof(buf), "\033[90m.\033[0m");  // Dim dot for off
        }
        return std::string(buf);
    }

    /**
     * Render a single device panel into the frame buffer.
     * @param isSelected Whether this device is currently selected (affects header color)
     */
    void renderDevicePanel(DeviceInstance& device, bool isSelected = false) {
        const DeviceDiagnostics diag = gatherDiagnostics(device, displayMirrorEnabled);

        // Build LED string with colored blocks
        std::string ledStr = "| LEDs: L[";
        for (int i = 8; i >= 0; i--) {
            ledStr += renderLEDStr(diag.leftLights[i]);
        }
        ledStr += "] T[";
        ledStr += renderLEDStr(diag.transmitLight);
        ledStr += "] R[";
        for (int i = 8; i >= 0; i--) {
            ledStr += renderLEDStr(diag.rightLights[i]);
        }
        char brightBuf[32];
        snprintf(brightBuf, sizeof(brightBuf), "]  Bright=%3d", diag.globalBrightness);
        ledStr += brightBuf;

        // Build LED debug string showing key indices with brightness
        const std::vector<LEDState::SingleLEDState>& leftLeds = diag.leftLights;
        char rgbBuf[200];
        snprintf(rgbBuf, sizeof(rgbBuf), "|   L0=(%d,%d,%d)@%d  L3=(%d,%d,%d)@%d  L5=(%d,%d,%d)@%d  L8=(%d,%d,%d)@%d",
                 leftLeds[0].color.red, leftLeds[0].color.green, leftLeds[0].color.blue, leftLeds[0].brightness,
                 leftLeds[3].color.red, leftLeds[3].color.green, leftLeds[3].color.blue, leftLeds[3].brightness,
                 leftLeds[5].color.red, leftLeds[5].color.green, leftLeds[5].color.blue, leftLeds[5].brightness,
                 leftLeds[8].color.red, leftLeds[8].color.green, leftLeds[8].color.blue, leftLeds[8].brightness);
        std::string ledDebugStr = rgbBuf;

        // Buffer all lines - use brighter color for selected device
        const char* headerColor = isSelected ? "\033[1;33m" : "\033[33m";  // Bold yellow vs dim yellow
        const char* selectedMarker = isSelected ? " *SELECTED*" : "";
        bufferLine(format("%s+-- Device [%s] %-6s%s -------------------------------+\033[0m",
                          headerColor,
                          diag.deviceId.c_str(),
                          diag.isHunter ? "HUNTER" : "BOUNTY",
                          selectedMarker));

        bufferLine(format("| Player: ID=%-4s  Allegiance=%-10s  W=%d L=%d Streak=%d",
                          diag.userId.c_str(),
                          diag.allegiance.c_str(),
                          diag.wins,
                          diag.losses,
                          diag.streak));

        bufferLine(format("| State: [%2d] %-20s  Game: Quickdraw",
                          diag.stateId, diag.stateName.c_str()));

        bufferLine(format("| History: %s", diag.stateHistory.c_str()));

        // Display info
        if (displayMirrorEnabled) {
            bufferLine(format("| Display: Font=%-6s    Mirror=ON",
                              diag.fontModeName.c_str()));

            // Top border — with Captions header when enabled
            if (captionsEnabled) {
                bufferLine("|  +----------------------------------------------------------------+  Captions:");
            } else {
                bufferLine("|  +----------------------------------------------------------------+");
            }

            // Mirror lines — first 4 get captions alongside when enabled
            for (size_t i = 0; i < diag.asciiMirrorLines.size(); i++) {
                std::string line = "|  |" + diag.asciiMirrorLines[i] + "|";
                if (captionsEnabled && i < 4) {
                    std::string caption = diag.displayRows[i].empty() ? "" : diag.displayRows[i];
                    line += "  [" + std::to_string(i) + "] " + caption;
                }
                bufferLine(line);
            }

            bufferLine("|  +----------------------------------------------------------------+");
        } else if (captionsEnabled) {
            bufferLine(format("| Display: Font=%-6s  Captions:",
                              diag.fontModeName.c_str()));
            bufferLine(format("|                       [0] %s",
                              diag.displayRows[0].empty() ? "(blank)" : diag.displayRows[0].c_str()));
            bufferLine(format("|                       [1] %s", diag.displayRows[1].c_str()));
            bufferLine(format("|                       [2] %s", diag.displayRows[2].c_str()));
            bufferLine(format("|                       [3] %s", diag.displayRows[3].c_str()));
        } else {
            bufferLine(format("| Display: Font=%-6s",
                              diag.fontModeName.c_str()));
        }

        bufferLine(ledStr);
        bufferLine(ledDebugStr);

        bufferLine(format("| Haptics: %-3s  Intensity=%3d",
                          diag.hapticsOn ? "ON" : "OFF",
                          diag.hapticsIntensity));

        // Determine which jack is PRIMARY based on role
        const char* outPrimaryLabel = diag.isHunter ? " (PRIMARY)" : "";
        const char* inPrimaryLabel = diag.isHunter ? "" : " (PRIMARY)";

        bufferLine(format("| Serial OUT%s: in=%zu out=%zu%s",
                          outPrimaryLabel,
                          diag.outInputQueueSize,
                          diag.outOutputBufferSize,
                          diag.outConnLabel.c_str()));
        bufferLine(format("|   TX: %s", diag.outSentSummary.c_str()));
        bufferLine(format("|   RX: %s", diag.outRecvSummary.c_str()));

        bufferLine(format("| Serial IN%s:  in=%zu out=%zu%s",
                          inPrimaryLabel,
                          diag.inInputQueueSize,
                          diag.inOutputBufferSize,
                          diag.inConnLabel.c_str()));
        bufferLine(format("|   TX: %s", diag.inSentSummary.c_str()));
        bufferLine(format("|   RX: %s", diag.inRecvSummary.c_str()));

        bufferLine(format("| ESP-NOW: %s  MAC=%s  Peers=%zu  Pending=%zu",
                          diag.peerCommsState.c_str(),
                          diag.macString.c_str(),
                          diag.peerCount,
                          diag.pendingPacketCount));
        bufferLine(format("|   TX: %s", diag.espNowTxSummary.c_str()));
        bufferLine(format("|   RX: %s", diag.espNowRxSummary.c_str()));

        bufferLine(format("| HTTP: %s  Pending=%zu  Mock=%s",
                          diag.httpConnected ? "Connected" : "Disconnected",
                          diag.httpPendingRequests,
                          diag.mockServerOnline ? "ON" : "OFF"));

        bufferLine(format("|   Requests: %s", diag.httpRequestsSummary.c_str()));

        bufferLine(format("| Errors: %s", diag.errorSummary.c_str()));

        bufferLine(format("%s+--------------------------------------------------------------+\033[0m", headerColor));
    }
};

} // namespace cli

#endif // NATIVE_BUILD
