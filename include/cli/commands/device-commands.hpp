#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "command-result.hpp"
#include "cli/cli-device-types.hpp"

namespace cli {

class DeviceCommands {
public:
    static CommandResult cmdList(const std::vector<std::string>& tokens,
                                 const std::vector<DeviceInstance>& devices,
                                 int selectedDevice);

    static CommandResult cmdSelect(const std::vector<std::string>& tokens,
                                   const std::vector<DeviceInstance>& devices,
                                   int& selectedDevice);

    static CommandResult cmdState(const std::vector<std::string>& tokens,
                                  std::vector<DeviceInstance>& devices,
                                  int selectedDevice);

    static CommandResult cmdAdd(const std::vector<std::string>& tokens,
                                std::vector<DeviceInstance>& devices,
                                int& selectedDevice);

    static CommandResult cmdRole(const std::vector<std::string>& tokens,
                                 const std::vector<DeviceInstance>& devices,
                                 int selectedDevice);

    static CommandResult cmdReboot(const std::vector<std::string>& tokens,
                                   std::vector<DeviceInstance>& devices,
                                   int selectedDevice);
};

} // namespace cli

#endif // NATIVE_BUILD
