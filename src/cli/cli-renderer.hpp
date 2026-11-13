#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <sys/ioctl.h>

#include "cli/cli-terminal.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-diagnostics.hpp"
#include "device/drivers/peer-comms-types.hpp"

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

    /** Shows or hides the braille mirror of the device's display. */
    void setDisplayMirror(bool enabled) { displayMirrorEnabled = enabled; }
    /** Flips the display mirror. */
    void toggleDisplayMirror() { displayMirrorEnabled = !displayMirrorEnabled; }
    /** True while the display mirror is shown. */
    bool isDisplayMirrorEnabled() const { return displayMirrorEnabled; }

    /** Shows or hides the text the device last drew. */
    void setCaptions(bool enabled) { captionsEnabled = enabled; }
    /** Flips the captions. */
    void toggleCaptions() { captionsEnabled = !captionsEnabled; }
    /** True while captions are shown. */
    bool isCaptionsEnabled() const { return captionsEnabled; }

    /**
     * Render the full UI for all devices, command result, and prompt.
     */
    void renderUI(std::vector<DeviceInstance>& devices,
                  const std::string& commandResult,
                  const std::string& commandBuffer,
                  int selectedDeviceIndex) {
        currentFrame.clear();

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
            renderDevicePanel(
            gatherDiagnostics(devices[selectedDeviceIndex], displayMirrorEnabled));
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
    std::vector<std::string> previousFrame;
    std::vector<std::string> currentFrame;

    /**
     * Add a line to the current frame buffer.
     */
    void bufferLine(const std::string& line) { currentFrame.push_back(line); }

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
        while (previousFrame.size() < currentFrame.size()) {
            previousFrame.push_back("");
        }

        // Compare and update only changed lines (within terminal bounds)
        for (size_t i = 0; i < currentFrame.size(); i++) {
            int row = STATUS_START_ROW + static_cast<int>(i);
            if (row >= termHeight) break;

            if (i >= previousFrame.size() || currentFrame[i] != previousFrame[i]) {
                Terminal::moveCursor(row, 1);
                Terminal::clearLine();
                printf("%s", currentFrame[i].c_str());
            }
        }

        // Clear any extra lines from previous frame (within terminal bounds)
        if (previousFrame.size() > currentFrame.size()) {
            for (size_t i = currentFrame.size(); i < previousFrame.size(); i++) {
                int row = STATUS_START_ROW + static_cast<int>(i);
                if (row >= termHeight) break;
                Terminal::moveCursor(row, 1);
                Terminal::clearLine();
            }
        }

        // Store current frame as previous for next comparison
        previousFrame = currentFrame;
        currentFrame.clear();

        Terminal::flush();
    }
    
    /**
     * Helper to render LED as colored block.
     */
    static std::string renderLEDStr(const LEDState::SingleLEDState& led) {
        char buf[64];
        if (led.brightness > 0 && (led.color.red > 0 || led.color.green > 0 || led.color.blue > 0)) {
            int r = (led.color.red * led.brightness) / 255;
            int g = (led.color.green * led.brightness) / 255;
            int b = (led.color.blue * led.brightness) / 255;
            snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm \033[0m", r, g, b);
        } else {
            snprintf(buf, sizeof(buf), "\033[90m.\033[0m");  // Dim dot for off
        }
        return std::string(buf);
    }

    /**
     * Helper to truncate string for display.
     */
    static std::string truncate(const std::string& str, size_t maxLen) {
        if (str.length() <= maxLen) return str;
        return str.substr(0, maxLen - 2) + "..";
    }

    static std::string getPacketTypeName(PktType type) {
        switch (type) {
            case PktType::kPlayerInfoBroadcast: return "INFO";
            case PktType::kMatchCommand: return "CMD";
            default: return "?" + std::to_string(static_cast<int>(type));
        }
    }

    /** One "TYPE<arrow>MAC" entry per packet, most recent first, or "(none)". */
    static std::string formatPackets(const std::vector<PacketDiagnostics>& packets,
                                     const char* arrow) {
        if (packets.empty()) return "(none)";
        std::string text;
        for (size_t i = 0; i < packets.size(); i++) {
            if (i > 0) text += ", ";
            text += getPacketTypeName(packets[i].packetType);
            text += arrow + truncate(packets[i].peerMac, 8);
        }
        return text;
    }

    /** The cable marker beside a jack, empty when nothing is plugged in. */
    static std::string formatCableLabel(const JackDiagnostics& jack) {
        if (jack.cabledDevice < 0) return "";
        return format(" \033[32m<->[Dev%d]\033[0m", jack.cabledDevice);
    }

    /** Renders the selected device's panel into the frame buffer. */
    void renderDevicePanel(const DeviceDiagnostics& device) {
        // Build state history string
        std::string historyStr;
        if (device.stateHistory.empty()) {
            historyStr = "(none)";
        } else {
            for (size_t i = 0; i < device.stateHistory.size(); i++) {
                if (i > 0) historyStr += " -> ";
                historyStr += getStateName(device.stateHistory[i]);
            }
        }

        // Build error log string
        std::string errorStr = "(none)";
        for (size_t i = 0; i < device.recentErrors.size(); i++) {
            if (i == 0) errorStr = "";
            if (i > 0) errorStr += ", ";
            errorStr += truncate(device.recentErrors[i], 25);
        }

        // Captions are always laid out; the mirror lines exist only when it is on
        std::vector<std::string> displayRows(PANEL_DISPLAY_TEXT_ROWS, "");
        for (size_t i = 0; i < device.displayText.size(); i++) {
            displayRows[i] = truncate(device.displayText[i], 40);
        }

        // Build LED string with colored blocks
        std::string ledStr = "| LEDs: L[";
        for (int i = 8; i >= 0; i--) {
            ledStr += renderLEDStr(device.leftLights[i]);
        }
        ledStr += "] T[";
        ledStr += renderLEDStr(device.transmitLight);
        ledStr += "] R[";
        for (int i = 8; i >= 0; i--) {
            ledStr += renderLEDStr(device.rightLights[i]);
        }
        char brightBuf[32];
        snprintf(brightBuf, sizeof(brightBuf), "]  Bright=%3d", device.globalBrightness);
        ledStr += brightBuf;

        // Build LED debug string showing key indices with brightness
        const std::array<LEDState::SingleLEDState, 9>& leftLeds = device.leftLights;
        char rgbBuf[200];
        snprintf(rgbBuf, sizeof(rgbBuf), "|   L0=(%d,%d,%d)@%d  L3=(%d,%d,%d)@%d  L5=(%d,%d,%d)@%d  L8=(%d,%d,%d)@%d",
                 leftLeds[0].color.red, leftLeds[0].color.green, leftLeds[0].color.blue, leftLeds[0].brightness,
                 leftLeds[3].color.red, leftLeds[3].color.green, leftLeds[3].color.blue, leftLeds[3].brightness,
                 leftLeds[5].color.red, leftLeds[5].color.green, leftLeds[5].color.blue, leftLeds[5].brightness,
                 leftLeds[8].color.red, leftLeds[8].color.green, leftLeds[8].color.blue, leftLeds[8].brightness);
        std::string ledDebugStr = rgbBuf;

        // Buffer all lines
        const char* headerColor = "\033[1;33m";
        const char* selectedMarker = " *SELECTED*";
        bufferLine(format("%s+-- Device [%s] %-6s%s -------------------------------+\033[0m",
                   headerColor,
                   device.deviceId.c_str(),
                   device.isHunter ? "HUNTER" : "BOUNTY",
                   selectedMarker));

        bufferLine(format("| Player: ID=%-4s  Allegiance=%-10s  W=%d L=%d Streak=%d",
                          device.userId.c_str(), device.allegiance.c_str(), device.wins,
                          device.losses, device.streak));

        bufferLine(format("| State: [%2d] %-20s  Game: Quickdraw", device.stateId,
                          getStateName(device.stateId)));

        bufferLine(format("| History: %s", historyStr.c_str()));
        
        // Display info
        if (displayMirrorEnabled) {
            bufferLine(format("| Display: Font=%-6s    Mirror=ON", device.fontModeName.c_str()));

            // Top border — with Captions header when enabled
            if (captionsEnabled) {
                bufferLine("|  +----------------------------------------------------------------+  Captions:");
            } else {
                bufferLine("|  +----------------------------------------------------------------+");
            }

            // Mirror lines — first 4 get captions alongside when enabled
            for (size_t i = 0; i < device.displayMirror.size(); i++) {
                std::string line = "|  |" + device.displayMirror[i] + "|";
                if (captionsEnabled && i < 4) {
                    std::string caption = displayRows[i].empty() ? "" : displayRows[i];
                    line += "  [" + std::to_string(i) + "] " + caption;
                }
                bufferLine(line);
            }

            bufferLine("|  +----------------------------------------------------------------+");
        } else if (captionsEnabled) {
            bufferLine(format("| Display: Font=%-6s  Captions:", device.fontModeName.c_str()));
            bufferLine(format("|                       [0] %s",
                       displayRows[0].empty() ? "(blank)" : displayRows[0].c_str()));
            bufferLine(format("|                       [1] %s", displayRows[1].c_str()));
            bufferLine(format("|                       [2] %s", displayRows[2].c_str()));
            bufferLine(format("|                       [3] %s", displayRows[3].c_str()));
        } else {
            bufferLine(format("| Display: Font=%-6s", device.fontModeName.c_str()));
        }

        bufferLine(ledStr);
        bufferLine(ledDebugStr);

        bufferLine(format("| Haptics: %-3s  Intensity=%3d", device.hapticsOn ? "ON" : "OFF",
                          device.hapticsIntensity));

        // Determine which jack is PRIMARY based on role
        const char* outPrimaryLabel = device.isHunter ? " (PRIMARY)" : "";
        const char* inPrimaryLabel = device.isHunter ? "" : " (PRIMARY)";

        bufferLine(format("| Serial OUT%s: in=%zu out=%zu%s", outPrimaryLabel,
                          device.outputJack.inputQueueSize, device.outputJack.outputBufferSize,
                          formatCableLabel(device.outputJack).c_str()));
        bufferLine(format("| Serial IN%s:  in=%zu out=%zu%s", inPrimaryLabel,
                          device.inputJack.inputQueueSize, device.inputJack.outputBufferSize,
                          formatCableLabel(device.inputJack).c_str()));

        bufferLine(format("| ESP-NOW: %s  MAC=%s  Peers=%zu  Pending=%zu",
                          device.peerCommsState == PeerCommsState::ENABLED ? "ENABLED" : "DISABLED",
                          device.macAddress.c_str(), device.peerCount, device.pendingPacketCount));
        bufferLine(format("|   TX: %s", formatPackets(device.sentPackets, "->").c_str()));
        bufferLine(format("|   RX: %s", formatPackets(device.receivedPackets, "<-").c_str()));

        // Build HTTP history string
        std::string httpStr = "(none)";
        for (size_t i = 0; i < device.httpRequests.size(); i++) {
            const HttpRequestDiagnostics& request = device.httpRequests[i];
            if (i == 0) httpStr = "";
            if (i > 0) httpStr += ", ";
            httpStr += request.method + " " + truncate(request.path, 20);
            httpStr += request.success ? " [OK]" : format(" [%d]", request.statusCode);
        }

        bufferLine(format("| HTTP: %s  Pending=%zu",
                          device.httpConnected ? "Connected" : "Disconnected",
                          device.httpPendingRequestCount));

        bufferLine(format("|   Requests: %s", httpStr.c_str()));
        
        bufferLine(format("| Errors: %s", errorStr.c_str()));
        
        bufferLine(format("%s+--------------------------------------------------------------+\033[0m", headerColor));
    }
};

} // namespace cli

#endif // NATIVE_BUILD
