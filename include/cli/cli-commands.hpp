#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <cstdlib>
#include <chrono>

#include "cli/cli-device.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/commands/command-result.hpp"
#include "cli/commands/command-utils.hpp"
#include "cli/commands/help-commands.hpp"
#include "cli/commands/button-commands.hpp"
#include "cli/commands/display-commands.hpp"
#include "cli/commands/device-commands.hpp"
#include "cli/commands/cable-commands.hpp"
#include "cli/commands/network-commands.hpp"
#include "cli/commands/game-commands.hpp"
#include "cli/commands/stats-commands.hpp"
#include "cli/commands/duel-commands.hpp"
#include "cli/commands/debug-commands.hpp"

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

        std::vector<std::string> tokens = CommandUtils::tokenize(cmd);
        if (tokens.empty()) {
            return result;
        }

        const std::string& command = tokens[0];

        // Dispatch to command handlers
        if (command == "help" || command == "h" || command == "?") {
            return HelpCommands::cmdHelp(tokens);
        }
        if (command == "help2") {
            return HelpCommands::cmdHelp2(tokens);
        }
        if (command == "quit" || command == "q" || command == "exit") {
            return HelpCommands::cmdQuit(tokens);
        }
        if (command == "list" || command == "ls") {
            return DeviceCommands::cmdList(tokens, devices, selectedDevice);
        }
        if (command == "select" || command == "sel") {
            return DeviceCommands::cmdSelect(tokens, devices, selectedDevice);
        }
        if (command == "b" || command == "button" || command == "click") {
            return ButtonCommands::cmdButton1Click(tokens, devices, selectedDevice);
        }
        if (command == "l" || command == "long" || command == "longpress") {
            return ButtonCommands::cmdButton1Long(tokens, devices, selectedDevice);
        }
        if (command == "b2" || command == "button2" || command == "click2") {
            return ButtonCommands::cmdButton2Click(tokens, devices, selectedDevice);
        }
        if (command == "l2" || command == "long2" || command == "longpress2") {
            return ButtonCommands::cmdButton2Long(tokens, devices, selectedDevice);
        }
        if (command == "state" || command == "st") {
            return DeviceCommands::cmdState(tokens, devices, selectedDevice);
        }
        if (command == "cable" || command == "connect" || command == "c") {
            return CableCommands::cmdCable(tokens, devices);
        }
        if (command == "peer" || command == "packet" || command == "espnow") {
            return NetworkCommands::cmdPeer(tokens, devices);
        }
        if (command == "inject") {
            return NetworkCommands::cmdInject(tokens, devices);
        }
        if (command == "add" || command == "new") {
            return DeviceCommands::cmdAdd(tokens, devices, selectedDevice);
        }
        if (command == "mirror" || command == "m") {
            return DisplayCommands::cmdMirror(tokens, renderer);
        }
        if (command == "captions" || command == "cap") {
            return DisplayCommands::cmdCaptions(tokens, renderer);
        }
        if (command == "display" || command == "d") {
            return DisplayCommands::cmdDisplay(tokens, renderer);
        }
        if (command == "reboot" || command == "restart") {
            return DeviceCommands::cmdReboot(tokens, devices, selectedDevice);
        }
        if (command == "role" || command == "roles") {
            return DeviceCommands::cmdRole(tokens, devices, selectedDevice);
        }
        if (command == "konami") {
            return GameCommands::cmdKonami(tokens, devices, selectedDevice);
        }
        if (command == "games") {
            return GameCommands::cmdGames(tokens);
        }
        if (command == "stats" || command == "info") {
            return StatsCommands::cmdStats(tokens, devices, selectedDevice);
        }
        if (command == "progress" || command == "prog") {
            return GameCommands::cmdProgress(tokens, devices, selectedDevice);
        }
        if (command == "colors" || command == "profiles") {
            return GameCommands::cmdColors(tokens, devices, selectedDevice);
        }
        if (command == "difficulty" || command == "diff") {
            return GameCommands::cmdDifficulty(tokens, devices, selectedDevice);
        }
        if (command == "demo") {
            return GameCommands::cmdDemo(tokens, devices, selectedDevice);
        }
        if (command == "debug") {
            return DebugCommands::cmdDebug(tokens, devices, selectedDevice, renderer);
        }
        if (command == "duel") {
            return DuelCommands::cmdDuel(tokens, devices, selectedDevice);
        }
        if (command == "rematch" || command == "r") {
            return DuelCommands::cmdRematch(tokens, devices, selectedDevice);
        }

        result.message = "Unknown command: " + command + " (try 'help')";
        return result;
    }
};

} // namespace cli

#endif // NATIVE_BUILD
