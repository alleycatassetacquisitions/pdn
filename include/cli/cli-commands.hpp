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
        if (command == "demo") {
            return cmdDemo(tokens, devices, selectedDevice);
        }
        if (command == "debug") {
            return cmdDebug(tokens, devices, selectedDevice, renderer);
        }

        result.message = "Unknown command: " + command + " (try 'help')";
        return result;
    }

private:
    // ==================== COMMAND HANDLERS ====================
    
    static CommandResult cmdHelp(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.message = "Keys: LEFT/RIGHT=select, UP/DOWN=buttons | Cmds: help, quit, list, select, add, b/l, b2/l2, cable, peer, display, mirror, captions, reboot, role, games, stats, progress, colors, demo";
        return result;
    }
    
    static CommandResult cmdHelp2(const std::vector<std::string>& /*tokens*/) {
        CommandResult result;
        result.message = "add [hunter|bounty|npc <game>|challenge <game>] - add device | cable <a> <b> - connect | peer <src> <dst> <type> - send packet | reboot [dev] - restart device | games - list games | stats [dev] [summary|detailed|streaks|reset] - FDN game statistics | progress [dev] - Konami grid | colors [dev] - color profiles | demo [game] [easy|hard] - play demo mode";
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
                                  std::vector<DeviceInstance>& devices,
                                  int selectedDevice) {
        CommandResult result;

        // Parse subcommand if present
        std::string subcommand = tokens.size() >= 2 ? tokens[1] : "summary";
        for (char& c : subcommand) {
            if (c >= 'A' && c <= 'Z') c += 32;  // tolower
        }

        // Check for reset subcommand
        if (subcommand == "reset") {
            return cmdStatsReset(tokens, devices, selectedDevice);
        }

        // Determine target device - check if first arg is a device ID
        int targetDevice = selectedDevice;
        int argOffset = 1;  // Start checking from token[1]

        // If subcommand is a valid device ID, use it
        int tempDevice = findDevice(subcommand, devices, -1);
        if (tempDevice >= 0) {
            targetDevice = tempDevice;
            subcommand = (tokens.size() >= 3) ? tokens[2] : "summary";
            for (char& c : subcommand) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }
            argOffset = 2;
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

        // Route to appropriate subcommand handler
        if (subcommand == "detailed") {
            return cmdStatsDetailed(dev, targetDevice);
        } else if (subcommand == "streaks") {
            return cmdStatsStreaks(dev, targetDevice);
        } else {
            // Default: summary
            return cmdStatsSummary(dev, targetDevice);
        }
    }

    static CommandResult cmdStatsSummary(DeviceInstance& dev, int deviceIndex) {
        CommandResult result;
        Player* p = dev.player;
        const GameStatsTracker& tracker = p->getGameStatsTracker();

        // Check if any games have been played
        if (!tracker.hasAnyGamesPlayed()) {
            result.message = "No FDN games played yet. Connect to an FDN device to start playing!";
            return result;
        }

        // Build table
        std::string table;
        table += "\n╔════════════════════════╦════════╦════════╦══════════╦══════════╦════════════╗\n";
        table += "║ Game                   ║  W/L   ║ Win %  ║ Avg Time ║ Best Time║   Streak   ║\n";
        table += "╠════════════════════════╬════════╬════════╬══════════╬══════════╬════════════╣\n";

        const GameType games[] = {
            GameType::GHOST_RUNNER, GameType::SPIKE_VECTOR, GameType::FIREWALL_DECRYPT,
            GameType::CIPHER_PATH, GameType::EXPLOIT_SEQUENCER, GameType::BREACH_DEFENSE,
            GameType::SIGNAL_ECHO
        };

        uint16_t totalWins = 0, totalLosses = 0;
        uint32_t overallBestTime = UINT32_MAX;
        uint16_t maxCurrentStreak = 0;
        uint16_t maxBestStreak = 0;
        bool hasCurrentWinStreak = false;

        for (const GameType& game : games) {
            const GameStats* combined = tracker.getCombinedStats(game);
            if (!combined || combined->totalAttempts == 0) continue;

            totalWins += combined->wins;
            totalLosses += combined->losses;

            if (combined->bestTimeMs < overallBestTime) {
                overallBestTime = combined->bestTimeMs;
            }

            if (combined->currentWinStreak > maxCurrentStreak) {
                maxCurrentStreak = combined->currentWinStreak;
                hasCurrentWinStreak = true;
            }

            if (combined->bestWinStreak > maxBestStreak) {
                maxBestStreak = combined->bestWinStreak;
            }

            // Format game name (pad to 22 chars)
            std::string gameName = getGameDisplayName(game);
            while (gameName.length() < 22) gameName += " ";

            // W/L (pad to 6 chars)
            char wlBuf[16];
            snprintf(wlBuf, sizeof(wlBuf), "%2d/%2d", combined->wins, combined->losses);
            std::string wl = wlBuf;
            while (wl.length() < 6) wl += " ";

            // Win % (pad to 6 chars)
            char winPctBuf[16];
            float winPct = tracker.getCombinedWinRate(game) * 100.0f;
            snprintf(winPctBuf, sizeof(winPctBuf), "%5.1f%%", winPct);
            std::string winPctStr = winPctBuf;

            // Avg Time (pad to 8 chars)
            uint32_t avgTime = tracker.getCombinedAverageTime(game);
            char avgBuf[16];
            snprintf(avgBuf, sizeof(avgBuf), "%5.1fs", avgTime / 1000.0f);
            std::string avgStr = avgBuf;
            while (avgStr.length() < 8) avgStr = " " + avgStr;

            // Best Time (pad to 8 chars)
            uint32_t bestTime = tracker.getCombinedBestTime(game);
            char bestBuf[16];
            if (bestTime == UINT32_MAX) {
                snprintf(bestBuf, sizeof(bestBuf), "    --");
            } else {
                snprintf(bestBuf, sizeof(bestBuf), "%5.1fs", bestTime / 1000.0f);
            }
            std::string bestStr = bestBuf;
            while (bestStr.length() < 8) bestStr = " " + bestStr;

            // Streak (pad to 10 chars)
            char streakBuf[32];
            char currentStreakChar = (combined->currentWinStreak > 0) ? 'W' : 'L';
            uint16_t currentStreakVal = (combined->currentWinStreak > 0) ?
                                        combined->currentWinStreak : combined->currentLossStreak;
            snprintf(streakBuf, sizeof(streakBuf), "%c%d (B:%d)",
                     currentStreakChar, currentStreakVal, combined->bestWinStreak);
            std::string streakStr = streakBuf;
            while (streakStr.length() < 10) streakStr += " ";

            table += "║ " + gameName + " ║ " + wl + " ║ " + winPctStr + " ║ " +
                     avgStr + " ║ " + bestStr + " ║ " + streakStr + " ║\n";
        }

        // Overall row
        table += "╠════════════════════════╬════════╬════════╬══════════╬══════════╬════════════╣\n";

        // Format overall stats
        std::string overallName = "OVERALL";
        while (overallName.length() < 22) overallName += " ";

        char wlBuf[16];
        snprintf(wlBuf, sizeof(wlBuf), "%2d/%2d", totalWins, totalLosses);
        std::string wl = wlBuf;
        while (wl.length() < 6) wl += " ";

        char winPctBuf[16];
        float totalPct = (totalWins + totalLosses > 0) ?
                         (100.0f * totalWins / (totalWins + totalLosses)) : 0.0f;
        snprintf(winPctBuf, sizeof(winPctBuf), "%5.1f%%", totalPct);

        GameStats overall = tracker.getOverallStats();
        char avgBuf[16];
        uint32_t overallAvg = (overall.totalAttempts > 0) ?
                              (overall.totalTimeMs / overall.totalAttempts) : 0;
        snprintf(avgBuf, sizeof(avgBuf), "%5.1fs", overallAvg / 1000.0f);
        std::string avgStr = avgBuf;
        while (avgStr.length() < 8) avgStr = " " + avgStr;

        char bestBuf[16];
        if (overallBestTime == UINT32_MAX) {
            snprintf(bestBuf, sizeof(bestBuf), "    --");
        } else {
            snprintf(bestBuf, sizeof(bestBuf), "%5.1fs", overallBestTime / 1000.0f);
        }
        std::string bestStr = bestBuf;
        while (bestStr.length() < 8) bestStr = " " + bestStr;

        char streakBuf[32];
        snprintf(streakBuf, sizeof(streakBuf), "%c%d (B:%d)",
                 hasCurrentWinStreak ? 'W' : '-',
                 maxCurrentStreak, maxBestStreak);
        std::string streakStr = streakBuf;
        while (streakStr.length() < 10) streakStr += " ";

        table += "║ " + overallName + " ║ " + wl + " ║ " + winPctBuf + " ║ " +
                 avgStr + " ║ " + bestStr + " ║ " + streakStr + " ║\n";

        table += "╚════════════════════════╩════════╩════════╩══════════╩══════════╩════════════╝\n";

        result.message = "Device " + std::to_string(deviceIndex) + " (" + dev.deviceId + ") FDN Game Stats:" + table;
        return result;
    }

    static CommandResult cmdStatsDetailed(DeviceInstance& dev, int deviceIndex) {
        CommandResult result;
        Player* p = dev.player;
        const GameStatsTracker& tracker = p->getGameStatsTracker();

        if (!tracker.hasAnyGamesPlayed()) {
            result.message = "No FDN games played yet. Connect to an FDN device to start playing!";
            return result;
        }

        std::string output = "Device " + std::to_string(deviceIndex) + " (" + dev.deviceId + ") Detailed FDN Stats:\n\n";

        const GameType games[] = {
            GameType::GHOST_RUNNER, GameType::SPIKE_VECTOR, GameType::FIREWALL_DECRYPT,
            GameType::CIPHER_PATH, GameType::EXPLOIT_SEQUENCER, GameType::BREACH_DEFENSE,
            GameType::SIGNAL_ECHO
        };

        for (const GameType& game : games) {
            const PerGameStats* perGame = tracker.getPerGameStats(game);
            if (!perGame || perGame->combined.totalAttempts == 0) continue;

            output += "┌─ " + std::string(getGameDisplayName(game)) + " ─────────────\n";

            // Easy mode
            if (perGame->easy.totalAttempts > 0) {
                char buf[256];
                float easyWinRate = (perGame->easy.totalAttempts > 0) ?
                                    (100.0f * perGame->easy.wins / perGame->easy.totalAttempts) : 0.0f;
                uint32_t easyAvg = (perGame->easy.totalAttempts > 0) ?
                                   (perGame->easy.totalTimeMs / perGame->easy.totalAttempts) : 0;
                uint32_t easyBest = perGame->easy.bestTimeMs;

                snprintf(buf, sizeof(buf),
                         "│ EASY:  W:%d L:%d (%5.1f%%) | Avg:%.1fs Best:%s | Streak:W%d (Best:%d)\n",
                         perGame->easy.wins, perGame->easy.losses, easyWinRate,
                         easyAvg / 1000.0f,
                         (easyBest == UINT32_MAX) ? "--" : (std::to_string(easyBest/1000) + "." + std::to_string((easyBest%1000)/100) + "s").c_str(),
                         perGame->easy.currentWinStreak, perGame->easy.bestWinStreak);
                output += buf;
            }

            // Hard mode
            if (perGame->hard.totalAttempts > 0) {
                char buf[256];
                float hardWinRate = (perGame->hard.totalAttempts > 0) ?
                                    (100.0f * perGame->hard.wins / perGame->hard.totalAttempts) : 0.0f;
                uint32_t hardAvg = (perGame->hard.totalAttempts > 0) ?
                                   (perGame->hard.totalTimeMs / perGame->hard.totalAttempts) : 0;
                uint32_t hardBest = perGame->hard.bestTimeMs;

                snprintf(buf, sizeof(buf),
                         "│ HARD:  W:%d L:%d (%5.1f%%) | Avg:%.1fs Best:%s | Streak:W%d (Best:%d)\n",
                         perGame->hard.wins, perGame->hard.losses, hardWinRate,
                         hardAvg / 1000.0f,
                         (hardBest == UINT32_MAX) ? "--" : (std::to_string(hardBest/1000) + "." + std::to_string((hardBest%1000)/100) + "s").c_str(),
                         perGame->hard.currentWinStreak, perGame->hard.bestWinStreak);
                output += buf;
            }

            output += "└────────────────────────\n\n";
        }

        result.message = output;
        return result;
    }

    static CommandResult cmdStatsStreaks(DeviceInstance& dev, int deviceIndex) {
        CommandResult result;
        Player* p = dev.player;
        const GameStatsTracker& tracker = p->getGameStatsTracker();

        if (!tracker.hasAnyGamesPlayed()) {
            result.message = "No FDN games played yet. Connect to an FDN device to start playing!";
            return result;
        }

        std::string output = "Device " + std::to_string(deviceIndex) + " (" + dev.deviceId + ") Streak Stats:\n\n";

        const GameType games[] = {
            GameType::GHOST_RUNNER, GameType::SPIKE_VECTOR, GameType::FIREWALL_DECRYPT,
            GameType::CIPHER_PATH, GameType::EXPLOIT_SEQUENCER, GameType::BREACH_DEFENSE,
            GameType::SIGNAL_ECHO
        };

        for (const GameType& game : games) {
            const GameStats* combined = tracker.getCombinedStats(game);
            if (!combined || combined->totalAttempts == 0) continue;

            char buf[128];
            char currentChar = (combined->currentWinStreak > 0) ? 'W' : 'L';
            uint16_t currentVal = (combined->currentWinStreak > 0) ?
                                  combined->currentWinStreak : combined->currentLossStreak;

            snprintf(buf, sizeof(buf), "%-22s Current: %c%-3d | Best Win: %-3d\n",
                     getGameDisplayName(game), currentChar, currentVal, combined->bestWinStreak);
            output += buf;
        }

        result.message = output;
        return result;
    }

    static CommandResult cmdStatsReset(const std::vector<std::string>& tokens,
                                       std::vector<DeviceInstance>& devices,
                                       int selectedDevice) {
        CommandResult result;

        // Determine target device
        int targetDevice = selectedDevice;
        if (tokens.size() >= 3) {
            targetDevice = findDevice(tokens[2], devices, -1);
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

        // Reset all game statistics
        dev.player->getGameStatsTracker().resetAll();
        result.message = "All FDN game statistics reset for device " + dev.deviceId;
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

    static CommandResult cmdDemo(const std::vector<std::string>& tokens,
                                 std::vector<DeviceInstance>& devices,
                                 int& selectedDevice) {
        CommandResult result;

        // Show help if no arguments
        if (tokens.size() < 2) {
            result.message = "Demo Mode - Play FDN Minigames\n\n"
                           "Usage: demo <game> [easy|hard]\n"
                           "       demo list - show all games\n"
                           "       demo all [easy|hard] - play all 7 games\n\n"
                           "Available games:\n"
                           "  signal-echo       - Memory sequence game\n"
                           "  ghost-runner      - Timing/reaction game\n"
                           "  spike-vector      - Targeting game\n"
                           "  firewall-decrypt  - Pattern matching puzzle\n"
                           "  cipher-path       - Pathfinding puzzle\n"
                           "  exploit-sequencer - Quick-time events\n"
                           "  breach-defense    - Defense strategy\n\n"
                           "Difficulty: easy (default), hard\n"
                           "Example: demo signal-echo hard";
            return result;
        }

        std::string subcommand = tokens[1];
        for (char& c : subcommand) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }

        // List games
        if (subcommand == "list") {
            result.message = "Available FDN Demo Games:\n\n"
                           "1. Signal Echo       - Repeat the light sequence from memory\n"
                           "2. Ghost Runner      - Hit the ghost when it enters the target zone\n"
                           "3. Spike Vector      - Target the moving spike walls\n"
                           "4. Firewall Decrypt  - Match the decryption pattern\n"
                           "5. Cipher Path       - Navigate the cipher maze\n"
                           "6. Exploit Sequencer - Execute the exploit chain\n"
                           "7. Breach Defense    - Defend against incoming threats\n\n"
                           "Use 'demo <game> [easy|hard]' to play";
            return result;
        }

        // Determine difficulty
        std::string difficulty = "easy";
        if (tokens.size() >= 3) {
            difficulty = tokens[2];
            for (char& c : difficulty) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }
            if (difficulty != "easy" && difficulty != "hard") {
                result.message = "Invalid difficulty: " + tokens[2] + " (use 'easy' or 'hard')";
                return result;
            }
        }

        // Play all games
        if (subcommand == "all") {
            result.message = "Demo Mode: Playing all 7 games at " + difficulty + " difficulty\n"
                           "Use 'add challenge <game>' to create a device for each game";
            return result;
        }

        // Parse game name
        GameType gameType;
        if (!parseGameName(subcommand, gameType)) {
            result.message = "Invalid game: " + tokens[1] + " - try 'demo list'";
            return result;
        }

        // Check if a demo device already exists
        int demoDeviceIndex = -1;
        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i].deviceId.find("DEMO-") == 0) {
                demoDeviceIndex = static_cast<int>(i);
                break;
            }
        }

        // Create or reuse demo device
        if (demoDeviceIndex < 0) {
            // Check max device limit
            static constexpr int MAX_DEVICES = 8;
            if (devices.size() >= MAX_DEVICES) {
                result.message = "Cannot create demo device (max " + std::to_string(MAX_DEVICES) + " devices)";
                return result;
            }

            // Create new challenge device
            int newIndex = static_cast<int>(devices.size());
            devices.push_back(DeviceFactory::createGameDevice(newIndex, subcommand));
            demoDeviceIndex = newIndex;
            selectedDevice = newIndex;

            // Rename to indicate demo mode
            devices[demoDeviceIndex].deviceId = "DEMO-" + std::to_string(newIndex);
        }

        result.message = std::string("Demo: ") + getGameDisplayName(gameType) + " (" + difficulty + ")\n" +
                       "Device " + devices[demoDeviceIndex].deviceId + " ready\n" +
                       "Press PRIMARY button to start the game\n" +
                       "Use 'b' or 'b2' commands to simulate button presses";

        return result;
    }

    static CommandResult cmdDebug(const std::vector<std::string>& tokens,
                                  std::vector<DeviceInstance>& devices,
                                  int& selectedDevice,
                                  Renderer& renderer) {
        CommandResult result;

        if (tokens.size() < 2) {
            result.message = "Usage: debug <subcommand> [args] - try 'debug help'";
            return result;
        }

        std::string subcommand = tokens[1];
        for (char& c : subcommand) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }

        if (subcommand == "help") {
            result.message = "Debug commands:\n"
                             "  debug set-npc-state <device_idx> <state>    - Force NPC state (NpcIdle, NpcHandshake, NpcGameActive, NpcReceiveResult)\n"
                             "  debug set-allegiance <device_idx> <value>    - Force allegiance (0=ALLEYCAT, 1=ENDLINE, 2=HELIX, 3=RESISTANCE)\n"
                             "  debug set-led <device_idx> <r> <g> <b>       - Force LED RGB values (0-255)\n"
                             "  debug help                                    - Show this help";
            return result;
        }

        if (subcommand == "set-npc-state") {
            if (tokens.size() < 4) {
                result.message = "Usage: debug set-npc-state <device_idx> <state>";
                return result;
            }

            int deviceIdx = findDevice(tokens[2], devices, -1);
            if (deviceIdx < 0 || deviceIdx >= static_cast<int>(devices.size())) {
                result.message = "Invalid device index: " + tokens[2];
                return result;
            }

            auto& dev = devices[deviceIdx];
            if (dev.deviceType != DeviceType::FDN) {
                result.message = "Device " + dev.deviceId + " is not an FDN/NPC device";
                return result;
            }

            std::string stateName = tokens[3];
            int targetStateId = -1;

            if (stateName == "NpcIdle" || stateName == "0") {
                targetStateId = 0;
            } else if (stateName == "NpcHandshake" || stateName == "1") {
                targetStateId = 1;
            } else if (stateName == "NpcGameActive" || stateName == "2") {
                targetStateId = 2;
            } else if (stateName == "NpcReceiveResult" || stateName == "3") {
                targetStateId = 3;
            } else {
                result.message = "Invalid NPC state: " + stateName + " (use NpcIdle, NpcHandshake, NpcGameActive, or NpcReceiveResult)";
                return result;
            }

            dev.game->skipToState(dev.pdn, targetStateId);
            result.message = "Device " + std::to_string(deviceIdx) + " NPC state set to " + stateName;
            return result;
        }

        if (subcommand == "set-allegiance") {
            if (tokens.size() < 4) {
                result.message = "Usage: debug set-allegiance <device_idx> <value>";
                return result;
            }

            int deviceIdx = findDevice(tokens[2], devices, -1);
            if (deviceIdx < 0 || deviceIdx >= static_cast<int>(devices.size())) {
                result.message = "Invalid device index: " + tokens[2];
                return result;
            }

            auto& dev = devices[deviceIdx];
            if (dev.deviceType != DeviceType::PLAYER) {
                result.message = "Device " + dev.deviceId + " is not a player device";
                return result;
            }

            if (!dev.player) {
                result.message = "Device " + dev.deviceId + " has no player object";
                return result;
            }

            std::string allegianceStr = tokens[3];
            Allegiance targetAllegiance;
            int allegianceNum = -1;

            if (allegianceStr == "ALLEYCAT" || allegianceStr == "0") {
                targetAllegiance = Allegiance::ALLEYCAT;
                allegianceNum = 0;
            } else if (allegianceStr == "ENDLINE" || allegianceStr == "1") {
                targetAllegiance = Allegiance::ENDLINE;
                allegianceNum = 1;
            } else if (allegianceStr == "HELIX" || allegianceStr == "2") {
                targetAllegiance = Allegiance::HELIX;
                allegianceNum = 2;
            } else if (allegianceStr == "RESISTANCE" || allegianceStr == "3") {
                targetAllegiance = Allegiance::RESISTANCE;
                allegianceNum = 3;
            } else {
                result.message = "Invalid allegiance: " + allegianceStr + " (use ALLEYCAT/0, ENDLINE/1, HELIX/2, or RESISTANCE/3)";
                return result;
            }

            dev.player->setAllegiance(targetAllegiance);

            char buf[128];
            snprintf(buf, sizeof(buf), "Device %d allegiance set to %s (%d)",
                     deviceIdx, allegianceStr.c_str(), allegianceNum);
            result.message = buf;
            return result;
        }

        if (subcommand == "set-led") {
            if (tokens.size() < 6) {
                result.message = "Usage: debug set-led <device_idx> <r> <g> <b>";
                return result;
            }

            int deviceIdx = findDevice(tokens[2], devices, -1);
            if (deviceIdx < 0 || deviceIdx >= static_cast<int>(devices.size())) {
                result.message = "Invalid device index: " + tokens[2];
                return result;
            }

            int r = std::atoi(tokens[3].c_str());
            int g = std::atoi(tokens[4].c_str());
            int b = std::atoi(tokens[5].c_str());

            if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
                result.message = "RGB values must be 0-255";
                return result;
            }

            auto& dev = devices[deviceIdx];
            LEDState::SingleLEDState ledState;
            ledState.color.red = static_cast<uint8_t>(r);
            ledState.color.green = static_cast<uint8_t>(g);
            ledState.color.blue = static_cast<uint8_t>(b);
            ledState.brightness = 255;

            dev.lightDriver->setLight(LightIdentifier::GLOBAL, 0, ledState);

            char buf[128];
            snprintf(buf, sizeof(buf), "Device %d LED set to (%d, %d, %d)",
                     deviceIdx, r, g, b);
            result.message = buf;
            return result;
        }

        result.message = "Unknown debug subcommand: " + subcommand + " (try 'debug help')";
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
