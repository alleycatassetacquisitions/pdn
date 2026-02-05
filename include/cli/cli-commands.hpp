#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <atomic>

#include "cli/cli-device.hpp"

namespace cli {

/**
 * Result of command execution.
 */
struct CommandResult {
    std::string message;      // Message to display to user
    bool shouldQuit = false;  // Whether the application should exit
};

/**
 * Command processor for CLI simulator.
 * Parses and executes user commands.
 */
class CommandProcessor {
public:
    CommandProcessor() = default;
    
    /**
     * Parse and execute a command string.
     * 
     * @param cmd The command string to execute
     * @param devices List of all devices
     * @param selectedDevice Reference to the selected device index (may be modified)
     * @return Result containing message and quit flag
     */
    CommandResult execute(const std::string& cmd,
                          std::vector<DeviceInstance>& devices,
                          int& selectedDevice) {
        CommandResult result;
        
        if (cmd.empty()) {
            return result;
        }
        
        // Parse command into tokens
        std::vector<std::string> tokens = tokenize(cmd);
        
        if (tokens.empty()) {
            return result;
        }
        
        const std::string& command = tokens[0];
        
        // Help command
        if (command == "help" || command == "h" || command == "?") {
            result.message = "Keys: UP=btn1, DOWN=btn2 | Cmds: help, quit, list, select <id>, b/l [id], b2/l2 [id], state";
            return result;
        }
        
        // Quit command
        if (command == "quit" || command == "q" || command == "exit") {
            result.shouldQuit = true;
            result.message = "Exiting...";
            return result;
        }
        
        // List devices
        if (command == "list" || command == "ls") {
            std::string msg = "Devices: ";
            for (size_t i = 0; i < devices.size(); i++) {
                if (i > 0) msg += ", ";
                msg += devices[i].deviceId;
                if (static_cast<int>(i) == selectedDevice) msg += "*";
            }
            result.message = msg;
            return result;
        }
        
        // Select device
        if (command == "select" || command == "sel" || command == "s") {
            if (tokens.size() < 2) {
                result.message = "Usage: select <device_id or index>";
                return result;
            }
            std::string target = tokens[1];
            for (size_t i = 0; i < devices.size(); i++) {
                if (devices[i].deviceId == target || std::to_string(i) == target) {
                    selectedDevice = static_cast<int>(i);
                    result.message = "Selected device " + devices[i].deviceId;
                    return result;
                }
            }
            result.message = "Device not found: " + target;
            return result;
        }
        
        // Button click (primary button)
        if (command == "p" || command == "primary" || command == "primaryClick") {
            int targetDevice = selectedDevice;
            if (tokens.size() >= 2) {
                targetDevice = findDevice(tokens[1], devices, selectedDevice);
            }
            if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
                devices[targetDevice].primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                result.message = "Button click on " + devices[targetDevice].deviceId;
            } else {
                result.message = "Invalid device";
            }
            return result;
        }
        
        // Long press (primary button)
        if (command == "pl" || command == "primaryLong" || command == "primaryLongPress") {
            int targetDevice = selectedDevice;
            if (tokens.size() >= 2) {
                targetDevice = findDevice(tokens[1], devices, selectedDevice);
            }
            if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
                devices[targetDevice].primaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
                result.message = "Long press on " + devices[targetDevice].deviceId;
            } else {
                result.message = "Invalid device";
            }
            return result;
        }
        
        // Secondary button click
        if (command == "s" || command == "secondary" || command == "secondaryClick") {
            int targetDevice = selectedDevice;
            if (tokens.size() >= 2) {
                targetDevice = findDevice(tokens[1], devices, selectedDevice);
            }
            if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
                devices[targetDevice].secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                result.message = "Button2 click on " + devices[targetDevice].deviceId;
            } else {
                result.message = "Invalid device";
            }
            return result;
        }
        
        // Secondary button long press
        if (command == "sl" || command == "secondaryLong" || command == "secondaryLongPress") {
            int targetDevice = selectedDevice;
            if (tokens.size() >= 2) {
                targetDevice = findDevice(tokens[1], devices, selectedDevice);
            }
            if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
                devices[targetDevice].secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
                result.message = "Button2 long press on " + devices[targetDevice].deviceId;
            } else {
                result.message = "Invalid device";
            }
            return result;
        }
        
        // State command - show detailed state
        if (command == "state" || command == "st") {
            int targetDevice = selectedDevice;
            if (tokens.size() >= 2) {
                targetDevice = findDevice(tokens[1], devices, selectedDevice);
            }
            if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
                auto& dev = devices[targetDevice];
                State* currentState = dev.game->getCurrentState();
                int stateId = currentState ? currentState->getStateId() : -1;
                result.message = dev.deviceId + ": " + getStateName(stateId);
            } else {
                result.message = "Invalid device";
            }
            return result;
        }
        
        result.message = "Unknown command: " + command + " (try 'help')";
        return result;
    }

private:
    /**
     * Tokenize a command string by spaces.
     */
    static std::vector<std::string> tokenize(const std::string& cmd) {
        std::vector<std::string> tokens;
        std::string token;
        for (char c : cmd) {
            if (c == ' ') {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += c;
            }
        }
        if (!token.empty()) {
            tokens.push_back(token);
        }
        return tokens;
    }
    
    /**
     * Find device by ID or index string.
     * Returns the found index or the default if not found.
     */
    static int findDevice(const std::string& target,
                          const std::vector<DeviceInstance>& devices,
                          int defaultDevice) {
        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i].deviceId == target || std::to_string(i) == target) {
                return static_cast<int>(i);
            }
        }
        return defaultDevice;
    }
};

} // namespace cli

#endif // NATIVE_BUILD
