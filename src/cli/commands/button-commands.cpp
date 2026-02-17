#ifdef NATIVE_BUILD

#include "cli/commands/button-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "device/drivers/native/native-button-driver.hpp"

namespace cli {

CommandResult ButtonCommands::cmdButton1Click(const std::vector<std::string>& tokens,
                                               std::vector<DeviceInstance>& devices,
                                               int selectedDevice) {
    CommandResult result;
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, selectedDevice);
    }
    if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
        devices[targetDevice].primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        result.message = "Button1 click on " + devices[targetDevice].deviceId;
    } else {
        result.message = "Invalid device";
    }
    return result;
}

CommandResult ButtonCommands::cmdButton1Long(const std::vector<std::string>& tokens,
                                              std::vector<DeviceInstance>& devices,
                                              int selectedDevice) {
    CommandResult result;
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, selectedDevice);
    }
    if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
        devices[targetDevice].primaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
        result.message = "Button1 long press on " + devices[targetDevice].deviceId;
    } else {
        result.message = "Invalid device";
    }
    return result;
}

CommandResult ButtonCommands::cmdButton2Click(const std::vector<std::string>& tokens,
                                               std::vector<DeviceInstance>& devices,
                                               int selectedDevice) {
    CommandResult result;
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, selectedDevice);
    }
    if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
        devices[targetDevice].secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        result.message = "Button2 click on " + devices[targetDevice].deviceId;
    } else {
        result.message = "Invalid device";
    }
    return result;
}

CommandResult ButtonCommands::cmdButton2Long(const std::vector<std::string>& tokens,
                                              std::vector<DeviceInstance>& devices,
                                              int selectedDevice) {
    CommandResult result;
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, selectedDevice);
    }
    if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
        devices[targetDevice].secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
        result.message = "Button2 long press on " + devices[targetDevice].deviceId;
    } else {
        result.message = "Invalid device";
    }
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
