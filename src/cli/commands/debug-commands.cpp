#ifdef NATIVE_BUILD

#include "cli/commands/debug-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "state/state-machine.hpp"
#include "game/player.hpp"
#include "device/pdn.hpp"
#include <chrono>
#include <cstdio>

// External cycling state (defined in cli-main.cpp)
extern bool g_panelCycling;
extern int g_panelCycleInterval;
extern std::chrono::steady_clock::time_point g_panelCycleLastSwitch;

extern bool g_stateCycling;
extern int g_stateCycleDevice;
extern int g_stateCycleInterval;
extern int g_stateCycleStep;
extern std::chrono::steady_clock::time_point g_stateCycleLastSwitch;

namespace cli {

CommandResult DebugCommands::cmdDebug(const std::vector<std::string>& tokens,
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
                         "  debug cycle-panels [interval_ms]             - Toggle auto-cycling through device panels (default: 2000ms)\n"
                         "  debug cycle-states <device_idx> [interval_ms] - Toggle cycling device through game states (default: 3000ms)\n"
                         "  debug show-state [device_idx]                - Show device state summary (type, state, allegiance, LED)\n"
                         "  debug help                                    - Show this help";
        return result;
    }

    if (subcommand == "set-npc-state") {
        if (tokens.size() < 4) {
            result.message = "Usage: debug set-npc-state <device_idx> <state>";
            return result;
        }

        int deviceIdx = CommandUtils::findDevice(tokens[2], devices, -1);
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

        int deviceIdx = CommandUtils::findDevice(tokens[2], devices, -1);
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

        int deviceIdx = CommandUtils::findDevice(tokens[2], devices, -1);
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

    if (subcommand == "cycle-panels") {
        // Parse optional interval
        if (tokens.size() >= 3) {
            int interval = std::atoi(tokens[2].c_str());
            if (interval < 100) {
                result.message = "Interval must be >= 100ms";
                return result;
            }
            g_panelCycleInterval = interval;
        }

        // Toggle cycling
        g_panelCycling = !g_panelCycling;

        if (g_panelCycling) {
            g_panelCycleLastSwitch = std::chrono::steady_clock::now();
            result.message = "Panel cycling ENABLED (interval: " +
                            std::to_string(g_panelCycleInterval) + "ms)";
        } else {
            result.message = "Panel cycling DISABLED";
        }
        return result;
    }

    if (subcommand == "cycle-states") {
        if (tokens.size() < 3) {
            result.message = "Usage: debug cycle-states <device_idx> [interval_ms]";
            return result;
        }

        int deviceIdx = CommandUtils::findDevice(tokens[2], devices, -1);
        if (deviceIdx < 0 || deviceIdx >= static_cast<int>(devices.size())) {
            result.message = "Invalid device index: " + tokens[2];
            return result;
        }

        // Parse optional interval
        if (tokens.size() >= 4) {
            int interval = std::atoi(tokens[3].c_str());
            if (interval < 100) {
                result.message = "Interval must be >= 100ms";
                return result;
            }
            g_stateCycleInterval = interval;
        }

        // Toggle cycling for this device
        if (g_stateCycling && g_stateCycleDevice == deviceIdx) {
            // Stop cycling
            g_stateCycling = false;
            g_stateCycleDevice = -1;
            g_stateCycleStep = 0;
            result.message = "State cycling DISABLED for device " + std::to_string(deviceIdx);
        } else {
            // Start cycling
            g_stateCycling = true;
            g_stateCycleDevice = deviceIdx;
            g_stateCycleStep = 0;
            g_stateCycleLastSwitch = std::chrono::steady_clock::now();
            result.message = "State cycling ENABLED for device " + std::to_string(deviceIdx) +
                            " (interval: " + std::to_string(g_stateCycleInterval) + "ms)";
        }
        return result;
    }

    if (subcommand == "show-state") {
        int targetDevice = selectedDevice;
        if (tokens.size() >= 3) {
            targetDevice = CommandUtils::findDevice(tokens[2], devices, -1);
        }

        if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
            result.message = "Invalid device index";
            return result;
        }

        auto& dev = devices[targetDevice];
        State* currentState = dev.game->getCurrentState();
        int stateId = currentState ? currentState->getStateId() : -1;
        std::string stateName = getStateName(stateId, dev.deviceType);

        // Get device type string
        std::string typeStr = (dev.deviceType == DeviceType::PLAYER) ? "PLAYER" : "FDN";

        // Get LED color (read from light driver)
        auto ledState = dev.lightDriver->getLight(LightIdentifier::GLOBAL, 0);
        int r = ledState.color.red;
        int g = ledState.color.green;
        int b = ledState.color.blue;

        char buf[256];
        if (dev.deviceType == DeviceType::PLAYER && dev.player) {
            // Show allegiance for player devices
            std::string allegianceStr = dev.player->getAllegianceString();
            snprintf(buf, sizeof(buf), "Device %d (%s): Type=%s State=%s Allegiance=%s LED=(%d,%d,%d)",
                     targetDevice, dev.deviceId.c_str(), typeStr.c_str(),
                     stateName.c_str(), allegianceStr.c_str(), r, g, b);
        } else {
            // FDN devices don't have allegiance
            snprintf(buf, sizeof(buf), "Device %d (%s): Type=%s State=%s LED=(%d,%d,%d)",
                     targetDevice, dev.deviceId.c_str(), typeStr.c_str(),
                     stateName.c_str(), r, g, b);
        }

        result.message = buf;
        return result;
    }

    result.message = "Unknown debug subcommand: " + subcommand + " (try 'debug help')";
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
