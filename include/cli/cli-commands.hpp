#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <cstdlib>

#include "cli/cli-device.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

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
     */
    CommandResult execute(const std::string& cmd,
                          std::vector<DeviceInstance>& devices,
                          int& selectedDevice,
                          Renderer& renderer) {
        CommandResult result;
        
        if (cmd.empty()) {
            return result;
        }
        
        std::vector<std::string> tokens = tokenize(cmd);
        if (tokens.empty()) {
            return result;
        }
        
        const std::string& command = tokens[0];
        
        // Dispatch to command handlers
        if (command == "help" || command == "h" || command == "?") {
            return cmdHelp(tokens);
        }
        if (command == "help2") {
            return cmdHelp2(tokens);
        }
        if (command == "quit" || command == "q" || command == "exit") {
            return cmdQuit(tokens);
        }
        if (command == "list" || command == "ls") {
            return cmdList(tokens, devices, selectedDevice);
        }
        if (command == "select" || command == "sel") {
            return cmdSelect(tokens, devices, selectedDevice);
        }
        if (command == "b" || command == "button" || command == "click") {
            return cmdButton1Click(tokens, devices, selectedDevice);
        }
        if (command == "l" || command == "long" || command == "longpress") {
            return cmdButton1Long(tokens, devices, selectedDevice);
        }
        if (command == "b2" || command == "button2" || command == "click2") {
            return cmdButton2Click(tokens, devices, selectedDevice);
        }
        if (command == "l2" || command == "long2" || command == "longpress2") {
            return cmdButton2Long(tokens, devices, selectedDevice);
        }
        if (command == "state" || command == "st") {
            return cmdState(tokens, devices, selectedDevice);
        }
        if (command == "cable" || command == "connect" || command == "c") {
            return cmdCable(tokens, devices);
        }
        if (command == "peer" || command == "packet" || command == "espnow") {
            return cmdPeer(tokens, devices);
        }
        if (command == "inject") {
            return cmdInject(tokens, devices);
        }
        if (command == "add" || command == "new") {
            return cmdAdd(tokens, devices, selectedDevice);
        }
        if (command == "mirror" || command == "m") {
            return cmdMirror(tokens, renderer);
        }
        if (command == "captions" || command == "cap") {
            return cmdCaptions(tokens, renderer);
        }
        if (command == "display" || command == "d") {
            return cmdDisplay(tokens, renderer);
        }
        if (command == "reboot" || command == "restart") {
            return cmdReboot(tokens, devices, selectedDevice);
        }
        if (command == "konami") {
            return cmdKonami(tokens, devices, selectedDevice);
        }

        result.message = "Unknown command: " + command + " (try 'help')";
        return result;
    }

private:
    // ==================== COMMAND HANDLERS ====================
    
    static CommandResult cmdHelp(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.message = "Keys: LEFT/RIGHT=select, UP/DOWN=buttons | Cmds: help, quit, list, select, add, b/l, b2/l2, cable, peer, display, mirror, captions, reboot";
        return result;
    }
    
    static CommandResult cmdHelp2(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.message = "add [hunter|bounty] - add device | cable <a> <b> - connect | peer <src> <dst> <type> - send packet | reboot [dev] - restart device";
        return result;
    }
    
    static CommandResult cmdQuit(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.shouldQuit = true;
        result.message = "Exiting...";
        return result;
    }
    
    static CommandResult cmdList(const std::vector<std::string>& /*tokens*/,
                                 const std::vector<DeviceInstance>& devices,
                                 int selectedDevice) {
        CommandResult result;
        std::string msg = "Devices: ";
        for (size_t i = 0; i < devices.size(); i++) {
            if (i > 0) msg += ", ";
            msg += devices[i].deviceId;
            if (static_cast<int>(i) == selectedDevice) msg += "*";
            
            int connected = SerialCableBroker::getInstance().getConnectedDevice(static_cast<int>(i));
            if (connected >= 0 && connected < static_cast<int>(devices.size())) {
                msg += "<->" + devices[connected].deviceId;
            }
        }
        result.message = msg;
        return result;
    }
    
    static CommandResult cmdSelect(const std::vector<std::string>& tokens,
                                   const std::vector<DeviceInstance>& devices,
                                   int& selectedDevice) {
        CommandResult result;
        if (tokens.size() < 2) {
            result.message = "Usage: select <device_id or index>";
            return result;
        }
        const std::string& target = tokens[1];
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
    
    static CommandResult cmdButton1Click(const std::vector<std::string>& tokens,
                                         std::vector<DeviceInstance>& devices,
                                         int selectedDevice) {
        CommandResult result;
        int targetDevice = selectedDevice;
        if (tokens.size() >= 2) {
            targetDevice = findDevice(tokens[1], devices, selectedDevice);
        }
        if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
            devices[targetDevice].primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            result.message = "Button1 click on " + devices[targetDevice].deviceId;
        } else {
            result.message = "Invalid device";
        }
        return result;
    }
    
    static CommandResult cmdButton1Long(const std::vector<std::string>& tokens,
                                        std::vector<DeviceInstance>& devices,
                                        int selectedDevice) {
        CommandResult result;
        int targetDevice = selectedDevice;
        if (tokens.size() >= 2) {
            targetDevice = findDevice(tokens[1], devices, selectedDevice);
        }
        if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
            devices[targetDevice].primaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
            result.message = "Button1 long press on " + devices[targetDevice].deviceId;
        } else {
            result.message = "Invalid device";
        }
        return result;
    }
    
    static CommandResult cmdButton2Click(const std::vector<std::string>& tokens,
                                         std::vector<DeviceInstance>& devices,
                                         int selectedDevice) {
        CommandResult result;
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
    
    static CommandResult cmdButton2Long(const std::vector<std::string>& tokens,
                                        std::vector<DeviceInstance>& devices,
                                        int selectedDevice) {
        CommandResult result;
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
    
    static CommandResult cmdState(const std::vector<std::string>& tokens,
                                  std::vector<DeviceInstance>& devices,
                                  int selectedDevice) {
        CommandResult result;
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
    
    static CommandResult cmdCable(const std::vector<std::string>& tokens,
                                  const std::vector<DeviceInstance>& devices) {
        CommandResult result;
        
        if (tokens.size() < 2) {
            result.message = "Usage: cable <devA> <devB> | cable -d <devA> <devB> | cable -l";
            return result;
        }
        
        // List connections
        if (tokens[1] == "-l" || tokens[1] == "list") {
            return cmdCableList(devices);
        }
        
        // Disconnect
        if (tokens[1] == "-d" || tokens[1] == "disconnect") {
            return cmdCableDisconnect(tokens, devices);
        }
        
        // Connect
        return cmdCableConnect(tokens, devices);
    }
    
    static CommandResult cmdCableList(const std::vector<DeviceInstance>& devices) {
        CommandResult result;
        const auto& connections = SerialCableBroker::getInstance().getConnections();
        if (connections.empty()) {
            result.message = "No cable connections";
        } else {
            result.message = "Cables: ";
            for (size_t i = 0; i < connections.size(); i++) {
                if (i > 0) result.message += ", ";
                const auto& conn = connections[i];
                std::string typeStr = conn.sameRole ? "[P-A]" : "[P-P]";
                result.message += devices[conn.deviceA].deviceId + 
                                  "<->" + 
                                  devices[conn.deviceB].deviceId +
                                  typeStr;
            }
        }
        return result;
    }
    
    static CommandResult cmdCableDisconnect(const std::vector<std::string>& tokens,
                                            const std::vector<DeviceInstance>& devices) {
        CommandResult result;
        if (tokens.size() < 4) {
            result.message = "Usage: cable -d <deviceA> <deviceB>";
            return result;
        }
        int devA = findDevice(tokens[2], devices, -1);
        int devB = findDevice(tokens[3], devices, -1);
        if (devA < 0 || devB < 0) {
            result.message = "Invalid device(s)";
            return result;
        }
        if (SerialCableBroker::getInstance().disconnect(devA, devB)) {
            result.message = "Disconnected " + devices[devA].deviceId + " from " + devices[devB].deviceId;
        } else {
            result.message = "Not connected";
        }
        return result;
    }
    
    static CommandResult cmdCableConnect(const std::vector<std::string>& tokens,
                                         const std::vector<DeviceInstance>& devices) {
        CommandResult result;
        if (tokens.size() < 3) {
            result.message = "Usage: cable <deviceA> <deviceB>";
            return result;
        }
        int devA = findDevice(tokens[1], devices, -1);
        int devB = findDevice(tokens[2], devices, -1);
        if (devA < 0 || devB < 0) {
            result.message = "Invalid device(s)";
            return result;
        }
        if (devA == devB) {
            result.message = "Cannot connect device to itself";
            return result;
        }
        if (SerialCableBroker::getInstance().connect(devA, devB)) {
            bool sameRole = (devices[devA].isHunter == devices[devB].isHunter);
            std::string typeStr = sameRole ? " (Primary-to-Auxiliary)" : " (Primary-to-Primary)";
            result.message = "Connected " + devices[devA].deviceId + " <-> " + devices[devB].deviceId + typeStr;
        } else {
            result.message = "Failed to connect";
        }
        return result;
    }
    
    static CommandResult cmdPeer(const std::vector<std::string>& tokens,
                                 const std::vector<DeviceInstance>& devices) {
        CommandResult result;
        
        if (tokens.size() < 4) {
            result.message = "Usage: peer <src> <dst|broadcast> <type> [hexdata]";
            return result;
        }
        
        int srcDevice = findDevice(tokens[1], devices, -1);
        if (srcDevice < 0) {
            result.message = "Invalid source device: " + tokens[1];
            return result;
        }
        
        // Get destination MAC
        uint8_t dstMac[6];
        bool isBroadcast = (tokens[2] == "broadcast" || tokens[2] == "bc" || tokens[2] == "*");
        int dstDevice = -1;
        
        if (isBroadcast) {
            std::memcpy(dstMac, NativePeerBroker::getInstance().getBroadcastAddress(), 6);
        } else {
            dstDevice = findDevice(tokens[2], devices, -1);
            if (dstDevice < 0) {
                result.message = "Invalid destination device: " + tokens[2];
                return result;
            }
            std::string macStr = devices[dstDevice].peerCommsDriver->getMacString();
            parseMacString(macStr, dstMac);
        }
        
        // Parse packet type
        int pktTypeInt = std::atoi(tokens[3].c_str());
        PktType packetType = static_cast<PktType>(pktTypeInt);
        
        // Parse optional hex data
        std::vector<uint8_t> data = parseHexData(tokens, 4);
        
        // Get source MAC and send
        std::string srcMacStr = devices[srcDevice].peerCommsDriver->getMacString();
        uint8_t srcMac[6];
        parseMacString(srcMacStr, srcMac);
        
        NativePeerBroker::getInstance().sendPacket(
            srcMac, dstMac, packetType,
            data.empty() ? nullptr : data.data(),
            data.size());
        
        std::string dstStr = isBroadcast ? "broadcast" : devices[dstDevice].deviceId;
        result.message = "Sent packet type " + std::to_string(pktTypeInt) + 
                         " from " + devices[srcDevice].deviceId + 
                         " to " + dstStr + 
                         " (" + std::to_string(data.size()) + " bytes)";
        return result;
    }
    
    static CommandResult cmdInject(const std::vector<std::string>& tokens,
                                   const std::vector<DeviceInstance>& devices) {
        CommandResult result;
        
        if (tokens.size() < 3) {
            result.message = "Usage: inject <dst> <type> [hexdata] - inject from external source";
            return result;
        }
        
        int dstDevice = findDevice(tokens[1], devices, -1);
        if (dstDevice < 0) {
            result.message = "Invalid destination device: " + tokens[1];
            return result;
        }
        
        int pktTypeInt = std::atoi(tokens[2].c_str());
        PktType packetType = static_cast<PktType>(pktTypeInt);
        
        std::vector<uint8_t> data = parseHexData(tokens, 3);
        
        // Create a fake external MAC address
        uint8_t externalMac[6] = {0xEE, 0xEE, 0xEE, 0x00, 0x00, 0x01};
        
        // Get destination MAC
        std::string dstMacStr = devices[dstDevice].peerCommsDriver->getMacString();
        uint8_t dstMac[6];
        parseMacString(dstMacStr, dstMac);
        
        NativePeerBroker::getInstance().sendPacket(
            externalMac, dstMac, packetType,
            data.empty() ? nullptr : data.data(),
            data.size());
        
        result.message = "Injected packet type " + std::to_string(pktTypeInt) + 
                         " to " + devices[dstDevice].deviceId +
                         " (" + std::to_string(data.size()) + " bytes)";
        return result;
    }
    
    static CommandResult cmdMirror(const std::vector<std::string>& tokens, Renderer& renderer) {
        CommandResult result;
        if (tokens.size() >= 2) {
            std::string arg = tokens[1];
            for (char& c : arg) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }
            if (arg == "on") {
                renderer.setDisplayMirror(true);
            } else if (arg == "off") {
                renderer.setDisplayMirror(false);
            } else {
                result.message = "Usage: mirror [on|off] - toggles without argument";
                return result;
            }
        } else {
            renderer.toggleDisplayMirror();
        }
        result.message = std::string("Display mirror ") + (renderer.isDisplayMirrorEnabled() ? "ON" : "OFF");
        return result;
    }

    static CommandResult cmdCaptions(const std::vector<std::string>& tokens, Renderer& renderer) {
        CommandResult result;
        if (tokens.size() >= 2) {
            std::string arg = tokens[1];
            for (char& c : arg) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }
            if (arg == "on") {
                renderer.setCaptions(true);
            } else if (arg == "off") {
                renderer.setCaptions(false);
            } else {
                result.message = "Usage: captions [on|off] - toggles without argument";
                return result;
            }
        } else {
            renderer.toggleCaptions();
        }
        result.message = std::string("Captions ") + (renderer.isCaptionsEnabled() ? "ON" : "OFF");
        return result;
    }

    static CommandResult cmdDisplay(const std::vector<std::string>& tokens, Renderer& renderer) {
        CommandResult result;
        if (tokens.size() >= 2) {
            std::string arg = tokens[1];
            for (char& c : arg) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }
            if (arg == "on") {
                renderer.setDisplayMirror(true);
                renderer.setCaptions(true);
            } else if (arg == "off") {
                renderer.setDisplayMirror(false);
                renderer.setCaptions(false);
            } else {
                result.message = "Usage: display [on|off] - toggles without argument";
                return result;
            }
        } else {
            // Toggle: if mirror and captions disagree, force both on
            if (renderer.isDisplayMirrorEnabled() != renderer.isCaptionsEnabled()) {
                renderer.setDisplayMirror(true);
                renderer.setCaptions(true);
            } else {
                renderer.toggleDisplayMirror();
                renderer.toggleCaptions();
            }
        }
        result.message = std::string("Display: Mirror=") +
                         (renderer.isDisplayMirrorEnabled() ? "ON" : "OFF") +
                         " Captions=" +
                         (renderer.isCaptionsEnabled() ? "ON" : "OFF");
        return result;
    }

    static CommandResult cmdReboot(const std::vector<std::string>& tokens,
                                   std::vector<DeviceInstance>& devices,
                                   int selectedDevice) {
        CommandResult result;
        int targetDevice = selectedDevice;
        if (tokens.size() >= 2) {
            targetDevice = findDevice(tokens[1], devices, selectedDevice);
        }
        if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
            result.message = "Invalid device";
            return result;
        }
        auto& dev = devices[targetDevice];

        // Ensure mock HTTP is ready for the new FetchUserData cycle
        dev.httpClientDriver->setMockServerEnabled(true);
        dev.httpClientDriver->setConnected(true);

        // Clear UI state history
        dev.stateHistory.clear();
        dev.lastStateId = -1;

        // skipToState(1) calls onStateDismounted on current state,
        // then onStateMounted on FetchUserData — same as initial boot
        dev.game->skipToState(dev.pdn, 1);

        result.message = "Rebooted " + dev.deviceId + " -> FetchUserData";
        return result;
    }

    static CommandResult cmdKonami(const std::vector<std::string>& tokens,
                                   std::vector<DeviceInstance>& devices,
                                   int selectedDevice) {
        CommandResult result;
        int targetDevice = selectedDevice;
        if (tokens.size() >= 3) {
            targetDevice = findDevice(tokens[1], devices, selectedDevice);
        }
        if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
            result.message = "Invalid device";
            return result;
        }
        auto& dev = devices[targetDevice];
        if (!dev.player) {
            result.message = "Device has no player (FDN devices don't have players)";
            return result;
        }
        if (tokens.size() < 2) {
            // Show current progress
            uint8_t progress = dev.player->getKonamiProgress();
            bool boon = dev.player->hasKonamiBoon();
            char buf[64];
            snprintf(buf, sizeof(buf), "Konami: 0x%02X (%d/7) boon=%s",
                     progress, __builtin_popcount(progress & 0x7F),
                     boon ? "yes" : "no");
            result.message = buf;
            return result;
        }
        // Set progress: konami <device> <value> OR konami <value>
        std::string valueStr = (tokens.size() >= 3) ? tokens[2] : tokens[1];
        int value = std::atoi(valueStr.c_str());
        dev.player->setKonamiProgress(static_cast<uint8_t>(value & 0xFF));

        // Auto-boon if all 7 set
        if (dev.player->isKonamiComplete()) {
            dev.player->setKonamiBoon(true);
        }

        uint8_t progress = dev.player->getKonamiProgress();
        bool boon = dev.player->hasKonamiBoon();
        char buf[64];
        snprintf(buf, sizeof(buf), "Konami set: 0x%02X (%d/7) boon=%s",
                 progress, __builtin_popcount(progress & 0x7F),
                 boon ? "yes" : "no");
        result.message = buf;
        return result;
    }

    static CommandResult cmdAdd(const std::vector<std::string>& tokens,
                                std::vector<DeviceInstance>& devices,
                                int& selectedDevice) {
        CommandResult result;
        
        // Check max device limit
        static constexpr int MAX_DEVICES = 8;
        if (devices.size() >= MAX_DEVICES) {
            result.message = "Cannot add more devices (max " + std::to_string(MAX_DEVICES) + ")";
            return result;
        }
        
        // Determine role - default to alternating based on current count
        bool isHunter = (devices.size() % 2 == 0);  // Even indices = hunter
        
        // Check for explicit role argument
        if (tokens.size() >= 2) {
            std::string role = tokens[1];
            // Convert to lowercase for comparison
            for (char& c : role) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }
            
            if (role == "hunter" || role == "h") {
                isHunter = true;
            } else if (role == "bounty" || role == "b") {
                isHunter = false;
            } else {
                result.message = "Usage: add [hunter|bounty] - role defaults to alternating";
                return result;
            }
        }
        
        // Create the new device
        int newIndex = static_cast<int>(devices.size());
        devices.push_back(DeviceFactory::createDevice(newIndex, isHunter));
        
        // Select the new device
        selectedDevice = newIndex;
        
        result.message = "Added " + devices.back().deviceId + 
                         " (" + (isHunter ? "Hunter" : "Bounty") + ") - now selected";
        return result;
    }

    // ==================== UTILITY FUNCTIONS ====================
    
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
    
    /**
     * Parse a MAC address string like "02:00:00:00:00:01" into bytes.
     */
    static void parseMacString(const std::string& macStr, uint8_t* macOut) {
        unsigned int bytes[6];
        sscanf(macStr.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]);
        for (int i = 0; i < 6; i++) {
            macOut[i] = static_cast<uint8_t>(bytes[i]);
        }
    }
    
    /**
     * Parse hex data from tokens starting at the given index.
     */
    static std::vector<uint8_t> parseHexData(const std::vector<std::string>& tokens, size_t startIndex) {
        std::vector<uint8_t> data;
        for (size_t i = startIndex; i < tokens.size(); i++) {
            unsigned int byte;
            if (tokens[i].find("0x") == 0 || tokens[i].find("0X") == 0) {
                byte = std::strtoul(tokens[i].c_str() + 2, nullptr, 16);
            } else {
                byte = std::strtoul(tokens[i].c_str(), nullptr, 16);
            }
            data.push_back(static_cast<uint8_t>(byte));
        }
        return data;
    }
};

} // namespace cli

#endif // NATIVE_BUILD
