#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <cstdlib>

#include "cli/cli-device.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include "game/quickdraw.hpp"
#include "game/progress-manager.hpp"

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
        if (command == "role" || command == "roles") {
            return cmdRole(tokens, devices, selectedDevice);
        }
        if (command == "konami") {
            return cmdKonami(tokens, devices, selectedDevice);
        }
        if (command == "games") {
            return cmdGames(tokens);
        }
        if (command == "stats" || command == "info") {
            return cmdStats(tokens, devices, selectedDevice);
        }
        if (command == "progress" || command == "prog") {
            return cmdProgress(tokens, devices, selectedDevice);
        }
        if (command == "colors" || command == "profiles") {
            return cmdColors(tokens, devices, selectedDevice);
        }

        result.message = "Unknown command: " + command + " (try 'help')";
        return result;
    }

private:
    // ==================== COMMAND HANDLERS ====================
    
    static CommandResult cmdHelp(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.message = "Keys: LEFT/RIGHT=select, UP/DOWN=buttons | Cmds: help, quit, list, select, add, b/l, b2/l2, cable, peer, display, mirror, captions, reboot, role, games, stats, progress, colors";
        return result;
    }
    
    static CommandResult cmdHelp2(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.message = "add [hunter|bounty|npc <game>|challenge <game>] - add device | cable <a> <b> - connect | peer <src> <dst> <type> - send packet | reboot [dev] - restart device | games - list games | stats [dev] - player stats | progress [dev] - Konami grid | colors [dev] - color profiles";
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
            } else if (role == "npc") {
                // add npc <game_name>
                if (tokens.size() < 3) {
                    result.message = "Usage: add npc <game> - try 'games' for valid game names";
                    return result;
                }

                std::string gameName = tokens[2];
                for (char& c : gameName) {
                    if (c >= 'A' && c <= 'Z') c += 32;
                }

                GameType gameType;
                if (!parseGameName(gameName, gameType)) {
                    result.message = "Invalid game: " + tokens[2] + " - try 'games' for valid game names";
                    return result;
                }

                int newIndex = static_cast<int>(devices.size());
                devices.push_back(DeviceFactory::createFdnDevice(newIndex, gameType));
                selectedDevice = newIndex;

                result.message = "Added NPC device " + devices.back().deviceId +
                                 " running " + getGameDisplayName(gameType) + " - now selected";
                return result;
            } else if (role == "challenge") {
                // add challenge <game_name>
                if (tokens.size() < 3) {
                    result.message = "Usage: add challenge <game> - try 'games' for valid game names";
                    return result;
                }

                std::string gameName = tokens[2];
                for (char& c : gameName) {
                    if (c >= 'A' && c <= 'Z') c += 32;
                }

                // Parse game name - use createGameDevice format (with hyphens)
                int newIndex = static_cast<int>(devices.size());
                devices.push_back(DeviceFactory::createGameDevice(newIndex, gameName));
                selectedDevice = newIndex;

                result.message = "Added challenge device " + devices.back().deviceId +
                                 " running " + gameName + " - now selected";
                return result;
            } else {
                result.message = "Usage: add [hunter|bounty|npc <game>|challenge <game>] - role defaults to alternating";
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

    static CommandResult cmdRole(const std::vector<std::string>& tokens,
                                 const std::vector<DeviceInstance>& devices,
                                 int selectedDevice) {
        CommandResult result;

        // Handle "role all" - show all devices
        if (tokens.size() >= 2 && tokens[1] == "all") {
            if (devices.empty()) {
                result.message = "No devices";
                return result;
            }
            std::string out;
            for (size_t i = 0; i < devices.size(); i++) {
                if (i > 0) out += " | ";
                out += "Device " + devices[i].deviceId +
                       " [" + std::to_string(i) + "]: " +
                       (devices[i].isHunter ? "Hunter" : "Bounty");
            }
            result.message = out;
            return result;
        }

        // Determine target device
        int targetDevice = selectedDevice;
        if (tokens.size() >= 2) {
            targetDevice = findDevice(tokens[1], devices, -1);
        }

        // Validate target device
        if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
            result.message = "Invalid device";
            return result;
        }

        // Show single device role
        result.message = "Device " + devices[targetDevice].deviceId + ": " +
                         (devices[targetDevice].isHunter ? "Hunter" : "Bounty");
        return result;
    }

    static CommandResult cmdGames(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.message = "Available games:\n"
                         "  quickdraw         - Quickdraw Duel\n"
                         "  ghost-runner      - Ghost Runner\n"
                         "  spike-vector      - Spike Vector\n"
                         "  firewall-decrypt  - Firewall Decrypt\n"
                         "  cipher-path       - Cipher Path\n"
                         "  exploit-sequencer - Exploit Sequencer\n"
                         "  breach-defense    - Breach Defense\n"
                         "  signal-echo       - Signal Echo";
        return result;
    }

    static CommandResult cmdStats(const std::vector<std::string>& tokens,
                                  const std::vector<DeviceInstance>& devices,
                                  int selectedDevice) {
        CommandResult result;

        // Determine target device
        int targetDevice = selectedDevice;
        if (tokens.size() >= 2) {
            targetDevice = findDevice(tokens[1], devices, -1);
        }

        // Validate target device
        if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
            result.message = "Invalid device";
            return result;
        }

        auto& dev = devices[targetDevice];

        // Check if device has a player (NPC/FDN devices don't)
        if (!dev.player) {
            result.message = "NPC device - no player stats";
            return result;
        }

        // Build stats summary
        Player* p = dev.player;
        char buf[512];

        // Basic info
        std::string roleStr = dev.isHunter ? "Hunter" : "Bounty";

        // Match stats
        int matches = p->getMatchesPlayed();
        int wins = p->getWins();
        int losses = p->getLosses();
        int streak = p->getStreak();

        // Reaction times
        unsigned long lastRT = p->getLastReactionTime();
        unsigned long avgRT = p->getAverageReactionTime();

        // Konami progress
        uint8_t konamiProg = p->getKonamiProgress();
        int konamiCount = __builtin_popcount(konamiProg & 0x7F);
        bool boon = p->hasKonamiBoon();

        // Color profile
        int equippedProfile = p->getEquippedColorProfile();
        std::string profileName;
        if (equippedProfile < 0) {
            profileName = dev.isHunter ? "HUNTER DEFAULT" : "BOUNTY DEFAULT";
        } else {
            profileName = getGameDisplayName(static_cast<GameType>(equippedProfile));
        }

        // Sync status (if Quickdraw game)
        std::string syncStatus = "N/A";
        if (dev.game && dev.game->getStateId() == 1) {  // QUICKDRAW_APP_ID = 1
            Quickdraw* quickdraw = static_cast<Quickdraw*>(dev.game);
            ProgressManager* progMgr = quickdraw->getProgressManager();
            if (progMgr) {
                syncStatus = progMgr->isSynced() ? "OK" : "PENDING";
            }
        }

        // Build per-game attempt breakdown
        std::string attemptBreakdown = "\n  FDN Attempts:";
        const char* gameNames[] = {"QD", "GR", "SV", "FD", "CP", "ES", "BD"};
        for (int i = 0; i < 7; i++) {
            GameType gt = static_cast<GameType>(i);
            uint8_t easy = p->getEasyAttempts(gt);
            uint8_t hard = p->getHardAttempts(gt);
            if (easy > 0 || hard > 0) {
                char gameBuf[64];
                snprintf(gameBuf, sizeof(gameBuf), "\n    %s: E=%d H=%d",
                         gameNames[i], easy, hard);
                attemptBreakdown += gameBuf;
            }
        }

        snprintf(buf, sizeof(buf),
                 "%s [%s] Stats:\n"
                 "  Matches: %d (W:%d L:%d) Streak:%d\n"
                 "  Reaction: Last=%lums Avg=%lums\n"
                 "  Konami: %d/7 unlocked (0x%02X) Boon:%s\n"
                 "  Color Profile: %s\n"
                 "  Sync: %s%s",
                 dev.deviceId.c_str(), roleStr.c_str(),
                 matches, wins, losses, streak,
                 lastRT, avgRT,
                 konamiCount, konamiProg, boon ? "Yes" : "No",
                 profileName.c_str(),
                 syncStatus.c_str(),
                 attemptBreakdown.c_str());

        result.message = buf;
        return result;
    }

    static CommandResult cmdProgress(const std::vector<std::string>& tokens,
                                     const std::vector<DeviceInstance>& devices,
                                     int selectedDevice) {
        CommandResult result;

        // Determine target device
        int targetDevice = selectedDevice;
        if (tokens.size() >= 2) {
            targetDevice = findDevice(tokens[1], devices, -1);
        }

        // Validate target device
        if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
            result.message = "Invalid device";
            return result;
        }

        auto& dev = devices[targetDevice];

        // Check if device has a player
        if (!dev.player) {
            result.message = "NPC device - no player stats";
            return result;
        }

        Player* p = dev.player;
        uint8_t progress = p->getKonamiProgress();
        bool boon = p->hasKonamiBoon();

        // Build visual grid
        std::string grid = dev.deviceId + " Konami Progress:\n\n";

        // Order: UP, DOWN, LEFT, RIGHT, B, A, START
        // Visual layout:
        //     [UP]
        // [L] [D] [R]
        //  [B] [A]
        //   [START]

        const char* buttonNames[7] = {"UP", "DOWN", "LEFT", "RIGHT", "B", "A", "START"};
        const char* gameNames[7] = {
            "Signal Echo",      // UP
            "Spike Vector",     // DOWN
            "Firewall Decrypt", // LEFT
            "Cipher Path",      // RIGHT
            "Exploit Seq",      // B
            "Breach Defense",   // A
            "Ghost Runner"      // START
        };

        // Build grid line by line
        //     [UP]
        bool hasUp = (progress & (1 << 0)) != 0;
        grid += "      [" + std::string(hasUp ? "X" : " ") + "] UP - " + gameNames[0] + "\n";

        // [L] [D] [R]
        bool hasLeft = (progress & (1 << 2)) != 0;
        bool hasDown = (progress & (1 << 1)) != 0;
        bool hasRight = (progress & (1 << 3)) != 0;
        grid += "  [" + std::string(hasLeft ? "X" : " ") + "] L   ";
        grid += "[" + std::string(hasDown ? "X" : " ") + "] D   ";
        grid += "[" + std::string(hasRight ? "X" : " ") + "] R\n";
        grid += "  " + std::string(gameNames[2]) + "  " + std::string(gameNames[1]) + "  " + std::string(gameNames[3]) + "\n";

        //  [B] [A]
        bool hasB = (progress & (1 << 4)) != 0;
        bool hasA = (progress & (1 << 5)) != 0;
        grid += "    [" + std::string(hasB ? "X" : " ") + "] B   ";
        grid += "[" + std::string(hasA ? "X" : " ") + "] A\n";
        grid += "    " + std::string(gameNames[4]) + "  " + std::string(gameNames[5]) + "\n";

        //   [START]
        bool hasStart = (progress & (1 << 6)) != 0;
        grid += "       [" + std::string(hasStart ? "X" : " ") + "] START\n";
        grid += "       " + std::string(gameNames[6]) + "\n";

        // Summary
        int count = __builtin_popcount(progress & 0x7F);
        grid += "\n" + std::to_string(count) + "/7 unlocked";
        if (boon) {
            grid += " | BOON ACTIVE";
        }

        result.message = grid;
        return result;
    }

    static CommandResult cmdColors(const std::vector<std::string>& tokens,
                                   const std::vector<DeviceInstance>& devices,
                                   int selectedDevice) {
        CommandResult result;

        // Determine target device
        int targetDevice = selectedDevice;
        if (tokens.size() >= 2) {
            targetDevice = findDevice(tokens[1], devices, -1);
        }

        // Validate target device
        if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
            result.message = "Invalid device";
            return result;
        }

        auto& dev = devices[targetDevice];

        // Check if device has a player
        if (!dev.player) {
            result.message = "NPC device - no player stats";
            return result;
        }

        Player* p = dev.player;
        int equipped = p->getEquippedColorProfile();
        const std::set<int>& eligible = p->getColorProfileEligibility();

        // Build color profile status
        std::string output = dev.deviceId + " Color Profiles:\n\n";

        // Default profile (always available)
        std::string defaultName = dev.isHunter ? "HUNTER DEFAULT" : "BOUNTY DEFAULT";
        output += "[" + std::string(equipped == -1 ? "*" : " ") + "] " + defaultName;
        output += " (default)\n";

        // Game profiles
        const GameType games[7] = {
            GameType::GHOST_RUNNER,
            GameType::SPIKE_VECTOR,
            GameType::FIREWALL_DECRYPT,
            GameType::CIPHER_PATH,
            GameType::EXPLOIT_SEQUENCER,
            GameType::BREACH_DEFENSE,
            GameType::SIGNAL_ECHO
        };

        for (int i = 0; i < 7; i++) {
            GameType game = games[i];
            int gameValue = static_cast<int>(game);
            bool isEligible = eligible.count(gameValue) > 0;
            bool isEquipped = (equipped == gameValue);

            std::string status;
            if (isEquipped) {
                status = "[*] ";
            } else if (isEligible) {
                status = "[ ] ";
            } else {
                status = "[X] ";
            }

            output += status + getGameDisplayName(game);

            if (!isEligible) {
                output += " (locked - beat hard mode to unlock)";
            } else if (isEquipped) {
                output += " (equipped)";
            }
            output += "\n";
        }

        result.message = output;
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
