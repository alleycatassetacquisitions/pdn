#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "cli/commands/command-result.hpp"
#include "cli/cli-device-types.hpp"

namespace cli {

/**
 * Game progression and minigame command handlers.
 */
class GameCommands {
public:
    /**
     * Show or manipulate Konami code progress.
     */
    static CommandResult cmdKonami(const std::vector<std::string>& tokens,
                                   std::vector<DeviceInstance>& devices,
                                   int selectedDevice);

    /**
     * List available games.
     */
    static CommandResult cmdGames(const std::vector<std::string>& tokens);

    /**
     * Show Konami progress grid visualization.
     */
    static CommandResult cmdProgress(const std::vector<std::string>& tokens,
                                     const std::vector<DeviceInstance>& devices,
                                     int selectedDevice);

    /**
     * Show color profile status and eligibility.
     */
    static CommandResult cmdColors(const std::vector<std::string>& tokens,
                                   const std::vector<DeviceInstance>& devices,
                                   int selectedDevice);

    /**
     * Show or reset difficulty auto-scaling.
     */
    static CommandResult cmdDifficulty(const std::vector<std::string>& tokens,
                                       const std::vector<DeviceInstance>& devices,
                                       int selectedDevice);

    /**
     * Start demo mode for FDN minigames.
     */
    static CommandResult cmdDemo(const std::vector<std::string>& tokens,
                                 std::vector<DeviceInstance>& devices,
                                 int& selectedDevice);
};

} // namespace cli

#endif // NATIVE_BUILD
