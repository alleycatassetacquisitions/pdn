#ifdef NATIVE_BUILD

#include "cli/commands/device-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "device/device-types.hpp"
#include "device/pdn.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"

namespace cli {

CommandResult DeviceCommands::cmdList(const std::vector<std::string>& /*tokens*/,
                                      const std::vector<DeviceInstance>& devices,
                                      int selectedDevice) {
    CommandResult result;
    std::string msg = "Devices: ";
    for (size_t i = 0; i < devices.size(); i++) {
        if (i > 0) msg += ", ";
        msg += devices[i].deviceId;
        if (static_cast<int>(i) == selectedDevice) msg += "*";
        
        int connected = SerialCableBroker::getInstance().getConnectedDevice(static_cast<int>(i));
        if (connected >= 0 && connected < static_cast<int>(devices.size())) {
            msg += "<->" + devices[connected].deviceId;
        }
    }
    result.message = msg;
    return result;
}

CommandResult DeviceCommands::cmdSelect(const std::vector<std::string>& tokens,
                                        const std::vector<DeviceInstance>& devices,
                                        int& selectedDevice) {
    CommandResult result;
    if (tokens.size() < 2) {
        result.message = "Usage: select <device_id or index>";
        return result;
    }
    const std::string& target = tokens[1];
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i].deviceId == target || std::to_string(i) == target) {
            selectedDevice = static_cast<int>(i);
            result.message = "Selected device " + devices[i].deviceId;
            return result;
        }
    }
    result.message = "Device not found: " + target;
    return result;
}

CommandResult DeviceCommands::cmdState(const std::vector<std::string>& tokens,
                                       std::vector<DeviceInstance>& devices,
                                       int selectedDevice) {
    CommandResult result;
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, selectedDevice);
    }
    if (targetDevice >= 0 && targetDevice < static_cast<int>(devices.size())) {
        auto& dev = devices[targetDevice];
        // Show the active app's state, not just the Quickdraw state
        StateMachine* activeApp = dev.pdn->getActiveApp();
        State* currentState = activeApp ? activeApp->getCurrentState() : nullptr;
        int stateId = currentState ? currentState->getStateId() : -1;
        result.message = dev.deviceId + ": " + getStateName(stateId, dev.deviceType);
    } else {
        result.message = "Invalid device";
    }
    return result;
}

CommandResult DeviceCommands::cmdAdd(const std::vector<std::string>& tokens,
                                     std::vector<DeviceInstance>& devices,
                                     int& selectedDevice) {
    CommandResult result;

    // Check max device limit
    static constexpr int MAX_DEVICES = 11;
    if (devices.size() >= MAX_DEVICES) {
        result.message = "Cannot add more devices (max " + std::to_string(MAX_DEVICES) + ")";
        return result;
    }

    // Determine role - default to alternating based on current count
    bool isHunter = (devices.size() % 2 == 0);  // Even indices = hunter

    // Check for explicit role argument
    if (tokens.size() >= 2) {
        std::string role = tokens[1];
        // Convert to lowercase for comparison
        for (char& c : role) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }

        if (role == "hunter" || role == "h") {
            isHunter = true;
        } else if (role == "bounty" || role == "b") {
            isHunter = false;
        } else if (role == "npc") {
            // add npc <game_name>
            if (tokens.size() < 3) {
                result.message = "Usage: add npc <game> - try 'games' for valid game names";
                return result;
            }

            std::string gameName = tokens[2];
            for (char& c : gameName) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }

            GameType gameType;
            if (!parseGameName(gameName, gameType)) {
                result.message = "Invalid game: " + tokens[2] + " - try 'games' for valid game names";
                return result;
            }

            int newIndex = static_cast<int>(devices.size());
            devices.push_back(DeviceFactory::createFdnDevice(newIndex, gameType));
            selectedDevice = newIndex;

            result.message = "Added NPC device " + devices.back().deviceId +
                             " running " + getGameDisplayName(gameType) + " - now selected";
            return result;
        } else if (role == "challenge") {
            // add challenge <game_name>
            if (tokens.size() < 3) {
                result.message = "Usage: add challenge <game> - try 'games' for valid game names";
                return result;
            }

            std::string gameName = tokens[2];
            for (char& c : gameName) {
                if (c >= 'A' && c <= 'Z') c += 32;
            }

            // Parse game name - use createGameDevice format (with hyphens)
            int newIndex = static_cast<int>(devices.size());
            devices.push_back(DeviceFactory::createGameDevice(newIndex, gameName));
            selectedDevice = newIndex;

            result.message = "Added challenge device " + devices.back().deviceId +
                             " running " + gameName + " - now selected";
            return result;
        } else {
            result.message = "Usage: add [hunter|bounty|npc <game>|challenge <game>] - role defaults to alternating";
            return result;
        }
    }

    // Create the new device
    int newIndex = static_cast<int>(devices.size());
    devices.push_back(DeviceFactory::createDevice(newIndex, isHunter));

    // Select the new device
    selectedDevice = newIndex;

    result.message = "Added " + devices.back().deviceId +
                     " (" + (isHunter ? "Hunter" : "Bounty") + ") - now selected";
    return result;
}

CommandResult DeviceCommands::cmdRole(const std::vector<std::string>& tokens,
                                      const std::vector<DeviceInstance>& devices,
                                      int selectedDevice) {
    CommandResult result;

    // Handle "role all" - show all devices
    if (tokens.size() >= 2 && tokens[1] == "all") {
        if (devices.empty()) {
            result.message = "No devices";
            return result;
        }
        std::string out;
        for (size_t i = 0; i < devices.size(); i++) {
            if (i > 0) out += " | ";
            out += "Device " + devices[i].deviceId +
                   " [" + std::to_string(i) + "]: " +
                   (devices[i].isHunter ? "Hunter" : "Bounty");
        }
        result.message = out;
        return result;
    }

    // Determine target device
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, -1);
    }

    // Validate target device
    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device";
        return result;
    }

    // Show single device role
    result.message = "Device " + devices[targetDevice].deviceId + ": " +
                     (devices[targetDevice].isHunter ? "Hunter" : "Bounty");
    return result;
}

CommandResult DeviceCommands::cmdReboot(const std::vector<std::string>& tokens,
                                        std::vector<DeviceInstance>& devices,
                                        int selectedDevice) {
    CommandResult result;
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, selectedDevice);
    }
    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device";
        return result;
    }
    auto& dev = devices[targetDevice];

    // Ensure mock HTTP is ready for the new FetchUserData cycle
    dev.httpClientDriver->setMockServerEnabled(true);
    dev.httpClientDriver->setConnected(true);

    // Clear UI state history
    dev.stateHistory.clear();
    dev.lastStateId = -1;

    // skipToState(1) calls onStateDismounted on current state,
    // then onStateMounted on FetchUserData — same as initial boot
    dev.game->skipToState(dev.pdn, 1);

    result.message = "Rebooted " + dev.deviceId + " -> FetchUserData";
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
