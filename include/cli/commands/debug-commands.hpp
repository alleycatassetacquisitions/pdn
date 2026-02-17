#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "cli/commands/command-result.hpp"
#include "cli/cli-device-types.hpp"
#include "cli/cli-renderer.hpp"

namespace cli {

/**
 * Debug and testing command handlers.
 */
class DebugCommands {
public:
    /**
     * Debug commands for testing and state manipulation.
     */
    static CommandResult cmdDebug(const std::vector<std::string>& tokens,
                                  std::vector<DeviceInstance>& devices,
                                  int& selectedDevice,
                                  Renderer& renderer);
};

} // namespace cli

#endif // NATIVE_BUILD
