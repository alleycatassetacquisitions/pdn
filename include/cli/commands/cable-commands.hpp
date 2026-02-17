#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "cli/commands/command-result.hpp"
#include "cli/cli-device-types.hpp"

namespace cli {

/**
 * Cable connection command handlers.
 */
class CableCommands {
public:
    /**
     * Main cable command dispatcher.
     */
    static CommandResult cmdCable(const std::vector<std::string>& tokens,
                                  const std::vector<DeviceInstance>& devices);

private:
    static CommandResult cmdCableList(const std::vector<DeviceInstance>& devices);
    
    static CommandResult cmdCableDisconnect(const std::vector<std::string>& tokens,
                                            const std::vector<DeviceInstance>& devices);
    
    static CommandResult cmdCableConnect(const std::vector<std::string>& tokens,
                                         const std::vector<DeviceInstance>& devices);
};

} // namespace cli

#endif // NATIVE_BUILD
