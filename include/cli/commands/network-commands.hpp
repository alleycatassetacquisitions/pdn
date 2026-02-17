#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "cli/commands/command-result.hpp"
#include "cli/cli-device-types.hpp"

namespace cli {

/**
 * Network/peer communication command handlers.
 */
class NetworkCommands {
public:
    /**
     * Send a peer (ESP-NOW) packet between devices.
     */
    static CommandResult cmdPeer(const std::vector<std::string>& tokens,
                                 const std::vector<DeviceInstance>& devices);

    /**
     * Inject a packet from an external source.
     */
    static CommandResult cmdInject(const std::vector<std::string>& tokens,
                                   const std::vector<DeviceInstance>& devices);
};

} // namespace cli

#endif // NATIVE_BUILD
