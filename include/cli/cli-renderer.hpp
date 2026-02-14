#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <sys/ioctl.h>

#include "cli/cli-terminal.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "wireless/peer-comms-types.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

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

    void setDisplayMirror(bool enabled) { displayMirrorEnabled_ = enabled; }
    void toggleDisplayMirror() { displayMirrorEnabled_ = !displayMirrorEnabled_; }
    bool isDisplayMirrorEnabled() const { return displayMirrorEnabled_; }

    void setCaptions(bool enabled) { captionsEnabled_ = enabled; }
    void toggleCaptions() { captionsEnabled_ = !captionsEnabled_; }
    bool isCaptionsEnabled() const { return captionsEnabled_; }

    /**
     * Handle terminal resize event (SIGWINCH).
     * Refreshes terminal dimensions and triggers a full redraw.
     */
    void handleResize() {
        getTerminalSize();
        // Clear screen and force full redraw
        Terminal::clearScreen();
        previousFrame_.clear();
    }

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
                    const char* typeLabel = devices[i].deviceType == DeviceType::FDN ? "NPC" : (devices[i].isHunter ? "H" : "B");
                    selectorBar += format("\033[1;7;33m[%d] %s %s\033[0m",
                                          static_cast<int>(i),
                                          devices[i].deviceId.c_str(),
                                          typeLabel);
                } else {
                    // Unselected device - dim
                    const char* typeLabel = devices[i].deviceType == DeviceType::FDN ? "NPC" : (devices[i].isHunter ? "H" : "B");
                    selectorBar += format("\033[90m[%d] %s %s\033[0m",
                                          static_cast<int>(i),
                                          devices[i].deviceId.c_str(),
                                          typeLabel);
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
    bool displayMirrorEnabled_ = false;
    bool captionsEnabled_ = true;
    std::vector<std::string> previousFrame_;
    std::vector<std::string> currentFrame_;
    int termWidth_ = 0;
    int termHeight_ = 0;
    
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
     * Query and update terminal dimensions.
     * Called on startup and when terminal is resized (SIGWINCH).
     */
    void getTerminalSize() {
        #ifndef _WIN32
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_col > 0) termWidth_ = static_cast<int>(ws.ws_col);
            if (ws.ws_row > 0) termHeight_ = static_cast<int>(ws.ws_row);
        }
        #endif
        // Set defaults if ioctl failed or on Windows
        if (termWidth_ == 0) termWidth_ = 120;
        if (termHeight_ == 0) termHeight_ = 50;
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
     * Helper to truncate string for display.
     */
    static std::string truncate(const std::string& str, size_t maxLen) {
        if (str.length() <= maxLen) return str;
        return str.substr(0, maxLen - 2) + "..";
    }
    
    /**
     * Get human-readable packet type name.
     */
    static std::string getPacketTypeName(PktType type) {
        switch (type) {
            case PktType::kPlayerInfoBroadcast: return "INFO";
            case PktType::kQuickdrawCommand: return "CMD";
            case PktType::kDebugPacket: return "DBG";
            default: return "?" + std::to_string(static_cast<int>(type));
        }
    }
    
    /**
     * Render FDN device panel (NPC devices hosting minigames).
     * Shows game type, reward button, NPC state, serial/LED/haptics info.
     */
    void renderFdnPanel(DeviceInstance& device, bool isSelected = false) {
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
                historyStr += getStateName(device.stateHistory[i], DeviceType::FDN);
            }
        }

        // Build serial history strings for OUT jack (FDN devices use OUTPUT_JACK as PRIMARY)
        std::string outSentStr = "(none)";
        const auto& outSent = device.serialOutDriver->getSentHistory();
        if (!outSent.empty()) {
            outSentStr = "";
            for (size_t i = 0; i < outSent.size() && i < 3; i++) {
                if (i > 0) outSentStr += ", ";
                outSentStr += truncate(outSent[outSent.size() - 1 - i], 15);
            }
        }

        std::string outRecvStr = "(none)";
        const auto& outRecv = device.serialOutDriver->getReceivedHistory();
        if (!outRecv.empty()) {
            outRecvStr = "";
            for (size_t i = 0; i < outRecv.size() && i < 3; i++) {
                if (i > 0) outRecvStr += ", ";
                outRecvStr += truncate(outRecv[outRecv.size() - 1 - i], 15);
            }
        }

        // Build serial history strings for IN jack
        std::string inSentStr = "(none)";
        const auto& inSent = device.serialInDriver->getSentHistory();
        if (!inSent.empty()) {
            inSentStr = "";
            for (size_t i = 0; i < inSent.size() && i < 3; i++) {
                if (i > 0) inSentStr += ", ";
                inSentStr += truncate(inSent[inSent.size() - 1 - i], 15);
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

        // Get FDN game info
        FdnGame* fdnGame = dynamic_cast<FdnGame*>(device.game);
        const char* gameName = fdnGame ? getGameDisplayName(fdnGame->getGameType()) : "UNKNOWN";
        const char* rewardName = fdnGame ? getKonamiButtonName(fdnGame->getReward()) : "?";

        // Get serial connection info from broker
        std::string outConnLabel = "";
        std::string inConnLabel = "";
        const auto& connections = SerialCableBroker::getInstance().getConnections();
        for (const auto& conn : connections) {
            if (conn.deviceA == device.deviceIndex || conn.deviceB == device.deviceIndex) {
                // This device is part of this connection
                bool isDeviceA = (conn.deviceA == device.deviceIndex);
                int otherDevice = isDeviceA ? conn.deviceB : conn.deviceA;
                JackType myJack = isDeviceA ? conn.jackA : conn.jackB;

                if (myJack == JackType::OUTPUT_JACK) {
                    outConnLabel = format(" \033[32m<->[Dev%d]\033[0m", otherDevice);
                } else {
                    inConnLabel = format(" \033[32m<->[Dev%d]\033[0m", otherDevice);
                }
            }
        }

        // Buffer all lines - use brighter color for selected device
        const char* headerColor = isSelected ? "\033[1;33m" : "\033[33m";
        const char* selectedMarker = isSelected ? " *SELECTED*" : "";
        bufferLine(format("%s+-- Device [%s] NPC%s ----------------------------------+\033[0m",
                   headerColor,
                   device.deviceId.c_str(),
                   selectedMarker));

        bufferLine(format("| NPC Device: Game=%-20s  Reward=%s",
                   gameName,
                   rewardName));

        bufferLine(format("| State: [%2d] %-20s",
                   stateId, getStateName(stateId, DeviceType::FDN)));

        bufferLine(format("| History: %s", historyStr.c_str()));

        bufferLine(ledStr);

        bufferLine(format("| Haptics: %-3s  Intensity=%3d",
                   device.hapticsDriver->isOn() ? "ON" : "OFF",
                   device.hapticsDriver->getIntensity()));

        // FDN devices use OUTPUT_JACK as PRIMARY (same as hunters)
        bufferLine(format("| Serial OUT (PRIMARY): in=%zu out=%zu%s",
                   device.serialOutDriver->getInputQueueSize(),
                   device.serialOutDriver->getOutputBufferSize(),
                   outConnLabel.c_str()));
        bufferLine(format("|   TX: %s", outSentStr.c_str()));
        bufferLine(format("|   RX: %s", outRecvStr.c_str()));

        bufferLine(format("| Serial IN:  in=%zu out=%zu%s",
                   device.serialInDriver->getInputQueueSize(),
                   device.serialInDriver->getOutputBufferSize(),
                   inConnLabel.c_str()));
        bufferLine(format("|   TX: %s", inSentStr.c_str()));
        bufferLine(format("|   RX: %s", inRecvStr.c_str()));

        bufferLine(format("%s+--------------------------------------------------------------+\033[0m", headerColor));
    }

    /**
     * Render a single device panel into the frame buffer.
     * @param isSelected Whether this device is currently selected (affects header color)
     */
    void renderDevicePanel(DeviceInstance& device, bool isSelected = false) {
        // FDN devices have a different panel structure (no player stats, different sections)
        if (device.deviceType == DeviceType::FDN) {
            renderFdnPanel(device, isSelected);
            return;
        }

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
        
        // Build serial history strings for OUT jack
        std::string outSentStr = "(none)";
        const auto& outSent = device.serialOutDriver->getSentHistory();
        if (!outSent.empty()) {
            outSentStr = "";
            for (size_t i = 0; i < outSent.size() && i < 3; i++) {
                if (i > 0) outSentStr += ", ";
                outSentStr += truncate(outSent[outSent.size() - 1 - i], 15);
            }
        }
        
        std::string outRecvStr = "(none)";
        const auto& outRecv = device.serialOutDriver->getReceivedHistory();
        if (!outRecv.empty()) {
            outRecvStr = "";
            for (size_t i = 0; i < outRecv.size() && i < 3; i++) {
                if (i > 0) outRecvStr += ", ";
                outRecvStr += truncate(outRecv[outRecv.size() - 1 - i], 15);
            }
        }
        
        // Build serial history strings for IN jack
        std::string inSentStr = "(none)";
        const auto& inSent = device.serialInDriver->getSentHistory();
        if (!inSent.empty()) {
            inSentStr = "";
            for (size_t i = 0; i < inSent.size() && i < 3; i++) {
                if (i > 0) inSentStr += ", ";
                inSentStr += truncate(inSent[inSent.size() - 1 - i], 15);
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
        
        // Build display content - always collect captions, optionally render mirror
        std::vector<std::string> asciiMirrorLines;
        std::vector<std::string> displayRows(4, "");

        const auto& textHistory = device.displayDriver->getTextHistory();
        for (size_t i = 0; i < textHistory.size() && i < 4; i++) {
            displayRows[i] = truncate(textHistory[i], 40);
        }

        if (displayMirrorEnabled_) {
            asciiMirrorLines = device.displayDriver->renderToBraille();
        }
        
        // Build ESP-NOW packet history strings (separate TX and RX)
        std::string espNowTxStr = "(none)";
        std::string espNowRxStr = "(none)";
        const auto& pktHistory = device.peerCommsDriver->getPacketHistory();
        
        // Collect TX packets
        int txCount = 0;
        for (auto it = pktHistory.rbegin(); it != pktHistory.rend() && txCount < 3; ++it) {
            if (it->isSent) {
                if (txCount == 0) espNowTxStr = "";
                if (txCount > 0) espNowTxStr += ", ";
                espNowTxStr += getPacketTypeName(it->packetType);
                espNowTxStr += "->" + truncate(it->dstMac, 8);
                txCount++;
            }
        }
        
        // Collect RX packets
        int rxCount = 0;
        for (auto it = pktHistory.rbegin(); it != pktHistory.rend() && rxCount < 3; ++it) {
            if (!it->isSent) {
                if (rxCount == 0) espNowRxStr = "";
                if (rxCount > 0) espNowRxStr += ", ";
                espNowRxStr += getPacketTypeName(it->packetType);
                espNowRxStr += "<-" + truncate(it->srcMac, 8);
                rxCount++;
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
        
        // Buffer all lines - use brighter color for selected device
        const char* headerColor = isSelected ? "\033[1;33m" : "\033[33m";  // Bold yellow vs dim yellow
        const char* selectedMarker = isSelected ? " *SELECTED*" : "";
        bufferLine(format("%s+-- Device [%s] %-6s%s -------------------------------+\033[0m",
                   headerColor,
                   device.deviceId.c_str(),
                   device.isHunter ? "HUNTER" : "BOUNTY",
                   selectedMarker));
        
        bufferLine(format("| Player: ID=%-4s  Allegiance=%-10s  W=%d L=%d Streak=%d",
                   device.player->getUserID().c_str(),
                   device.player->getAllegianceString().c_str(),
                   device.player->getWins(),
                   device.player->getLosses(),
                   device.player->getStreak()));
        
        bufferLine(format("| State: [%2d] %-20s  Game: Quickdraw",
                   stateId, getStateName(stateId)));
        
        bufferLine(format("| History: %s", historyStr.c_str()));
        
        // Display info
        if (displayMirrorEnabled_) {
            bufferLine(format("| Display: Font=%-6s    Mirror=ON",
                       device.displayDriver->getFontModeName().c_str()));

            // Top border — with Captions header when enabled
            if (captionsEnabled_) {
                bufferLine("|  +----------------------------------------------------------------+  Captions:");
            } else {
                bufferLine("|  +----------------------------------------------------------------+");
            }

            // Mirror lines — first 4 get captions alongside when enabled
            for (size_t i = 0; i < asciiMirrorLines.size(); i++) {
                std::string line = "|  |" + asciiMirrorLines[i] + "|";
                if (captionsEnabled_ && i < 4) {
                    std::string caption = displayRows[i].empty() ? "" : displayRows[i];
                    line += "  [" + std::to_string(i) + "] " + caption;
                }
                bufferLine(line);
            }

            bufferLine("|  +----------------------------------------------------------------+");
        } else if (captionsEnabled_) {
            bufferLine(format("| Display: Font=%-6s  Captions:",
                       device.displayDriver->getFontModeName().c_str()));
            bufferLine(format("|                       [0] %s",
                       displayRows[0].empty() ? "(blank)" : displayRows[0].c_str()));
            bufferLine(format("|                       [1] %s", displayRows[1].c_str()));
            bufferLine(format("|                       [2] %s", displayRows[2].c_str()));
            bufferLine(format("|                       [3] %s", displayRows[3].c_str()));
        } else {
            bufferLine(format("| Display: Font=%-6s",
                       device.displayDriver->getFontModeName().c_str()));
        }
        
        bufferLine(ledStr);
        bufferLine(ledDebugStr);
        
        bufferLine(format("| Haptics: %-3s  Intensity=%3d",
                   device.hapticsDriver->isOn() ? "ON" : "OFF",
                   device.hapticsDriver->getIntensity()));
        
        // Get serial connection info from broker
        std::string outConnLabel = "";
        std::string inConnLabel = "";
        const auto& connections = SerialCableBroker::getInstance().getConnections();
        for (const auto& conn : connections) {
            if (conn.deviceA == device.deviceIndex || conn.deviceB == device.deviceIndex) {
                // This device is part of this connection
                bool isDeviceA = (conn.deviceA == device.deviceIndex);
                int otherDevice = isDeviceA ? conn.deviceB : conn.deviceA;
                JackType myJack = isDeviceA ? conn.jackA : conn.jackB;
                
                if (myJack == JackType::OUTPUT_JACK) {
                    outConnLabel = format(" \033[32m<->[Dev%d]\033[0m", otherDevice);
                } else {
                    inConnLabel = format(" \033[32m<->[Dev%d]\033[0m", otherDevice);
                }
            }
        }
        
        // Determine which jack is PRIMARY based on role
        const char* outPrimaryLabel = device.isHunter ? " (PRIMARY)" : "";
        const char* inPrimaryLabel = device.isHunter ? "" : " (PRIMARY)";
        
        bufferLine(format("| Serial OUT%s: in=%zu out=%zu%s",
                   outPrimaryLabel,
                   device.serialOutDriver->getInputQueueSize(),
                   device.serialOutDriver->getOutputBufferSize(),
                   outConnLabel.c_str()));
        bufferLine(format("|   TX: %s", outSentStr.c_str()));
        bufferLine(format("|   RX: %s", outRecvStr.c_str()));
        
        bufferLine(format("| Serial IN%s:  in=%zu out=%zu%s",
                   inPrimaryLabel,
                   device.serialInDriver->getInputQueueSize(),
                   device.serialInDriver->getOutputBufferSize(),
                   inConnLabel.c_str()));
        bufferLine(format("|   TX: %s", inSentStr.c_str()));
        bufferLine(format("|   RX: %s", inRecvStr.c_str()));
        
        // Get broker stats
        size_t pendingPkts = NativePeerBroker::getInstance().getPendingPacketCount();
        size_t peerCount = NativePeerBroker::getInstance().getPeerCount();
        
        bufferLine(format("| ESP-NOW: %s  MAC=%s  Peers=%zu  Pending=%zu",
                   device.peerCommsDriver->getStateString().c_str(),
                   device.peerCommsDriver->getMacString().c_str(),
                   peerCount,
                   pendingPkts));
        bufferLine(format("|   TX: %s", espNowTxStr.c_str()));
        bufferLine(format("|   RX: %s", espNowRxStr.c_str()));
        
        // Build HTTP history string
        std::string httpStr = "(none)";
        const auto& httpHistory = device.httpClientDriver->getRequestHistory();
        if (!httpHistory.empty()) {
            httpStr = "";
            int count = 0;
            for (auto it = httpHistory.rbegin(); it != httpHistory.rend() && count < 3; ++it, ++count) {
                if (count > 0) httpStr += ", ";
                httpStr += it->method + " " + truncate(it->path, 20);
                httpStr += it->success ? " [OK]" : format(" [%d]", it->statusCode);
            }
        }
        
        bufferLine(format("| HTTP: %s  Pending=%zu  Mock=%s",
                   device.httpClientDriver->isConnected() ? "Connected" : "Disconnected",
                   device.httpClientDriver->getPendingRequestCount(),
                   device.httpClientDriver->isMockServerEnabled() ? "ON" : "OFF"));
        
        bufferLine(format("|   Requests: %s", httpStr.c_str()));
        
        bufferLine(format("| Errors: %s", errorStr.c_str()));
        
        bufferLine(format("%s+--------------------------------------------------------------+\033[0m", headerColor));
    }
};

} // namespace cli

#endif // NATIVE_BUILD
