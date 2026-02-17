#ifdef NATIVE_BUILD

#include "cli/commands/cable-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "cli/cli-serial-broker.hpp"

namespace cli {

CommandResult CableCommands::cmdCable(const std::vector<std::string>& tokens,
                                      const std::vector<DeviceInstance>& devices) {
    CommandResult result;
    
    if (tokens.size() < 2) {
        result.message = "Usage: cable <devA> <devB> | cable -d <devA> <devB> | cable -l";
        return result;
    }
    
    // List connections
    if (tokens[1] == "-l" || tokens[1] == "list") {
        return cmdCableList(devices);
    }
    
    // Disconnect
    if (tokens[1] == "-d" || tokens[1] == "disconnect") {
        return cmdCableDisconnect(tokens, devices);
    }
    
    // Connect
    return cmdCableConnect(tokens, devices);
}

CommandResult CableCommands::cmdCableList(const std::vector<DeviceInstance>& devices) {
    CommandResult result;
    const auto& connections = SerialCableBroker::getInstance().getConnections();
    if (connections.empty()) {
        result.message = "No cable connections";
    } else {
        result.message = "Cables: ";
        for (size_t i = 0; i < connections.size(); i++) {
            if (i > 0) result.message += ", ";
            const auto& conn = connections[i];
            std::string typeStr = conn.sameRole ? "[P-A]" : "[P-P]";
            result.message += devices[conn.deviceA].deviceId + 
                              "<->" + 
                              devices[conn.deviceB].deviceId +
                              typeStr;
        }
    }
    return result;
}

CommandResult CableCommands::cmdCableDisconnect(const std::vector<std::string>& tokens,
                                                const std::vector<DeviceInstance>& devices) {
    CommandResult result;
    if (tokens.size() < 4) {
        result.message = "Usage: cable -d <deviceA> <deviceB>";
        return result;
    }
    int devA = CommandUtils::findDevice(tokens[2], devices, -1);
    int devB = CommandUtils::findDevice(tokens[3], devices, -1);
    if (devA < 0 || devB < 0) {
        result.message = "Invalid device(s)";
        return result;
    }
    if (SerialCableBroker::getInstance().disconnect(devA, devB)) {
        result.message = "Disconnected " + devices[devA].deviceId + " from " + devices[devB].deviceId;
    } else {
        result.message = "Not connected";
    }
    return result;
}

CommandResult CableCommands::cmdCableConnect(const std::vector<std::string>& tokens,
                                             const std::vector<DeviceInstance>& devices) {
    CommandResult result;
    if (tokens.size() < 3) {
        result.message = "Usage: cable <deviceA> <deviceB>";
        return result;
    }
    int devA = CommandUtils::findDevice(tokens[1], devices, -1);
    int devB = CommandUtils::findDevice(tokens[2], devices, -1);
    if (devA < 0 || devB < 0) {
        result.message = "Invalid device(s)";
        return result;
    }
    if (devA == devB) {
        result.message = "Cannot connect device to itself";
        return result;
    }
    if (SerialCableBroker::getInstance().connect(devA, devB)) {
        bool sameRole = (devices[devA].isHunter == devices[devB].isHunter);
        std::string typeStr = sameRole ? " (Primary-to-Auxiliary)" : " (Primary-to-Primary)";
        result.message = "Connected " + devices[devA].deviceId + " <-> " + devices[devB].deviceId + typeStr;
    } else {
        result.message = "Failed to connect";
    }
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
