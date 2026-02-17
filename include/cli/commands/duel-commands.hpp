#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "cli/commands/command-result.hpp"
#include "cli/cli-device-types.hpp"

namespace cli {

/**
 * Duel tracking and rematch command handlers.
 */
class DuelCommands {
public:
    /**
     * Show duel history and records.
     */
    static CommandResult cmdDuel(const std::vector<std::string>& tokens,
                                 std::vector<DeviceInstance>& devices,
                                 int selectedDevice);

    /**
     * Initiate a rematch with the last opponent.
     */
    static CommandResult cmdRematch(const std::vector<std::string>& tokens,
                                    std::vector<DeviceInstance>& devices,
                                    int selectedDevice);
};

} // namespace cli

#endif // NATIVE_BUILD
