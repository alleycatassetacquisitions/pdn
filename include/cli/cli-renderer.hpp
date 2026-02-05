#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>

#include "cli/cli-terminal.hpp"
#include "cli/cli-device.hpp"

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
    
    /**
     * Render the full UI for all devices, command result, and prompt.
     */
    void renderUI(std::vector<DeviceInstance>& devices,
                  const std::string& commandResult,
                  const std::string& commandBuffer,
                  int selectedDeviceIndex) {
        // Build all device panels into the frame buffer
        currentFrame_.clear();
        
        for (auto& device : devices) {
            renderDevicePanel(device);
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
    std::vector<std::string> previousFrame_;
    std::vector<std::string> currentFrame_;
    
    /**
     * Add a line to the current frame buffer.
     */
    void bufferLine(const std::string& line) {
        currentFrame_.push_back(line);
    }
    
    /**
     * Render the current frame using differential updates.
     */
    void renderDiff() {
        // Ensure we have enough lines in previous frame for comparison
        while (previousFrame_.size() < currentFrame_.size()) {
            previousFrame_.push_back("");
        }
        
        // Compare and update only changed lines
        for (size_t i = 0; i < currentFrame_.size(); i++) {
            if (i >= previousFrame_.size() || currentFrame_[i] != previousFrame_[i]) {
                // Line changed - move to it and rewrite
                Terminal::moveCursor(STATUS_START_ROW + static_cast<int>(i), 1);
                Terminal::clearLine();
                printf("%s", currentFrame_[i].c_str());
            }
        }
        
        // Clear any extra lines from previous frame
        if (previousFrame_.size() > currentFrame_.size()) {
            for (size_t i = currentFrame_.size(); i < previousFrame_.size(); i++) {
                Terminal::moveCursor(STATUS_START_ROW + static_cast<int>(i), 1);
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
     * Helper to truncate string for display.
     */
    static std::string truncate(const std::string& str, size_t maxLen) {
        if (str.length() <= maxLen) return str;
        return str.substr(0, maxLen - 2) + "..";
    }
    
    /**
     * Render a single device panel into the frame buffer.
     */
    void renderDevicePanel(DeviceInstance& device) {
        State* currentState = device.game->getCurrentState();
        int stateId = currentState ? currentState->getStateId() : -1;
        
        // Update state history
        device.updateStateHistory(stateId);
        
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
        
        // Build serial history strings
        std::string outSentStr = "(none)";
        const auto& outSent = device.serialOutDriver->getSentHistory();
        if (!outSent.empty()) {
            outSentStr = "";
            for (size_t i = 0; i < outSent.size() && i < 3; i++) {
                if (i > 0) outSentStr += ", ";
                outSentStr += truncate(outSent[outSent.size() - 1 - i], 15);
            }
        }
        
        std::string inRecvStr = "(none)";
        const auto& inRecv = device.serialInDriver->getReceivedHistory();
        if (!inRecv.empty()) {
            inRecvStr = "";
            for (size_t i = 0; i < inRecv.size() && i < 3; i++) {
                if (i > 0) inRecvStr += ", ";
                inRecvStr += truncate(inRecv[inRecv.size() - 1 - i], 15);
            }
        }
        
        // Build error log string
        std::string errorStr = "(none)";
        const auto& logs = device.loggerDriver->getRecentLogs();
        int errorCount = 0;
        for (auto it = logs.rbegin(); it != logs.rend() && errorCount < 2; ++it) {
            if (it->level == LogLevel::ERROR) {
                if (errorCount == 0) errorStr = "";
                if (errorCount > 0) errorStr += ", ";
                errorStr += truncate(it->message, 25);
                errorCount++;
            }
        }
        
        // Build display text history (up to 4 rows)
        const auto& textHistory = device.displayDriver->getTextHistory();
        std::vector<std::string> displayRows(4, "");
        for (size_t i = 0; i < textHistory.size() && i < 4; i++) {
            displayRows[i] = truncate(textHistory[i], 50);
        }
        
        // Build ESP-NOW packet history string
        std::string espNowStr = "(none)";
        const auto& pktHistory = device.peerCommsDriver->getPacketHistory();
        if (!pktHistory.empty()) {
            espNowStr = "";
            int count = 0;
            for (auto it = pktHistory.rbegin(); it != pktHistory.rend() && count < 3; ++it, ++count) {
                if (count > 0) espNowStr += ", ";
                espNowStr += it->isSent ? "TX:" : "RX:";
                espNowStr += std::to_string(static_cast<int>(it->packetType));
            }
        }
        
        // Build LED string with colored blocks
        std::string ledStr = "| LEDs: L[";
        for (int i = 8; i >= 0; i--) {
            ledStr += renderLEDStr(device.lightDriver->getLeftLights()[i]);
        }
        ledStr += "] T[";
        ledStr += renderLEDStr(device.lightDriver->getTransmitLight());
        ledStr += "] R[";
        for (int i = 8; i >= 0; i--) {
            ledStr += renderLEDStr(device.lightDriver->getRightLights()[i]);
        }
        char brightBuf[32];
        snprintf(brightBuf, sizeof(brightBuf), "]  Bright=%3d", device.lightDriver->getGlobalBrightness());
        ledStr += brightBuf;
        
        // Build LED debug string showing key indices with brightness
        auto& leftLeds = device.lightDriver->getLeftLights();
        auto& rightLeds = device.lightDriver->getRightLights();
        char rgbBuf[200];
        snprintf(rgbBuf, sizeof(rgbBuf), "|   L0=(%d,%d,%d)@%d  L3=(%d,%d,%d)@%d  L5=(%d,%d,%d)@%d  L8=(%d,%d,%d)@%d",
                 leftLeds[0].color.red, leftLeds[0].color.green, leftLeds[0].color.blue, leftLeds[0].brightness,
                 leftLeds[3].color.red, leftLeds[3].color.green, leftLeds[3].color.blue, leftLeds[3].brightness,
                 leftLeds[5].color.red, leftLeds[5].color.green, leftLeds[5].color.blue, leftLeds[5].brightness,
                 leftLeds[8].color.red, leftLeds[8].color.green, leftLeds[8].color.blue, leftLeds[8].brightness);
        std::string ledDebugStr = rgbBuf;
        
        // Buffer all lines
        bufferLine(format("\033[1;33m+-- Device [%s] %-6s ---------------------------------------------+\033[0m",
                   device.deviceId.c_str(),
                   device.isHunter ? "HUNTER" : "BOUNTY"));
        
        bufferLine(format("| Player: ID=%-4s  Allegiance=%-10s  W=%d L=%d Streak=%d",
                   device.player->getUserID().c_str(),
                   device.player->getAllegianceString().c_str(),
                   device.player->getWins(),
                   device.player->getLosses(),
                   device.player->getStreak()));
        
        bufferLine(format("| State: [%2d] %-20s  Game: Quickdraw",
                   stateId, getStateName(stateId)));
        
        bufferLine(format("| History: %s", historyStr.c_str()));
        
        // Display info (4 rows showing text history, most recent first)
        bufferLine(format("| Display: Font=%-6s  [0] %s",
                   device.displayDriver->getFontModeName().c_str(),
                   displayRows[0].empty() ? "(blank)" : displayRows[0].c_str()));
        bufferLine(format("|                       [1] %s", displayRows[1].c_str()));
        bufferLine(format("|                       [2] %s", displayRows[2].c_str()));
        bufferLine(format("|                       [3] %s", displayRows[3].c_str()));
        
        bufferLine(ledStr);
        bufferLine(ledDebugStr);
        
        bufferLine(format("| Haptics: %-3s  Intensity=%3d",
                   device.hapticsDriver->isOn() ? "ON" : "OFF",
                   device.hapticsDriver->getIntensity()));
        
        bufferLine(format("| Serial OUT: in=%zu out=%zu  Sent: %s",
                   device.serialOutDriver->getInputQueueSize(),
                   device.serialOutDriver->getOutputBufferSize(),
                   outSentStr.c_str()));
        
        bufferLine(format("| Serial IN:  in=%zu out=%zu  Recv: %s",
                   device.serialInDriver->getInputQueueSize(),
                   device.serialInDriver->getOutputBufferSize(),
                   inRecvStr.c_str()));
        
        bufferLine(format("| ESP-NOW: %s  MAC=%s  Pkts: %s",
                   device.peerCommsDriver->getStateString().c_str(),
                   device.peerCommsDriver->getMacString().c_str(),
                   espNowStr.c_str()));
        
        bufferLine(format("| HTTP: %s",
                   device.httpClientDriver->isConnected() ? "Connected" : "Disconnected"));
        
        bufferLine(format("| Errors: %s", errorStr.c_str()));
        
        bufferLine("\033[1;33m+--------------------------------------------------------------+\033[0m");
    }
};

} // namespace cli

#endif // NATIVE_BUILD
