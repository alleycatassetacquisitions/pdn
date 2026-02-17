#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "command-result.hpp"
#include "cli/cli-device-types.hpp"

namespace cli {

class ButtonCommands {
public:
    static CommandResult cmdButton1Click(const std::vector<std::string>& tokens,
                                         std::vector<DeviceInstance>& devices,
                                         int selectedDevice);
    static CommandResult cmdButton1Long(const std::vector<std::string>& tokens,
                                        std::vector<DeviceInstance>& devices,
                                        int selectedDevice);
    static CommandResult cmdButton2Click(const std::vector<std::string>& tokens,
                                         std::vector<DeviceInstance>& devices,
                                         int selectedDevice);
    static CommandResult cmdButton2Long(const std::vector<std::string>& tokens,
                                        std::vector<DeviceInstance>& devices,
                                        int selectedDevice);
};

} // namespace cli

#endif // NATIVE_BUILD
