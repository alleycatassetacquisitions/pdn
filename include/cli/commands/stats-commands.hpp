#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "cli/commands/command-result.hpp"
#include "cli/cli-device-types.hpp"

namespace cli {

/**
 * Game statistics command handlers.
 */
class StatsCommands {
public:
    /**
     * Main stats command dispatcher.
     */
    static CommandResult cmdStats(const std::vector<std::string>& tokens,
                                  std::vector<DeviceInstance>& devices,
                                  int selectedDevice);

private:
    static CommandResult cmdStatsSummary(DeviceInstance& dev, int deviceIndex);
    static CommandResult cmdStatsDetailed(DeviceInstance& dev, int deviceIndex);
    static CommandResult cmdStatsStreaks(DeviceInstance& dev, int deviceIndex);
    static CommandResult cmdStatsReset(const std::vector<std::string>& tokens,
                                       std::vector<DeviceInstance>& devices,
                                       int selectedDevice);
};

} // namespace cli

#endif // NATIVE_BUILD
