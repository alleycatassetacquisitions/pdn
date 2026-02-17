#ifdef NATIVE_BUILD

#include "cli/commands/network-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include <cstring>

namespace cli {

CommandResult NetworkCommands::cmdPeer(const std::vector<std::string>& tokens,
                                       const std::vector<DeviceInstance>& devices) {
    CommandResult result;
    
    if (tokens.size() < 4) {
        result.message = "Usage: peer <src> <dst|broadcast> <type> [hexdata]";
        return result;
    }
    
    int srcDevice = CommandUtils::findDevice(tokens[1], devices, -1);
    if (srcDevice < 0) {
        result.message = "Invalid source device: " + tokens[1];
        return result;
    }
    
    // Get destination MAC
    uint8_t dstMac[6];
    bool isBroadcast = (tokens[2] == "broadcast" || tokens[2] == "bc" || tokens[2] == "*");
    int dstDevice = -1;
    
    if (isBroadcast) {
        std::memcpy(dstMac, NativePeerBroker::getInstance().getBroadcastAddress(), 6);
    } else {
        dstDevice = CommandUtils::findDevice(tokens[2], devices, -1);
        if (dstDevice < 0) {
            result.message = "Invalid destination device: " + tokens[2];
            return result;
        }
        std::string macStr = devices[dstDevice].peerCommsDriver->getMacString();
        CommandUtils::parseMacString(macStr, dstMac);
    }
    
    // Parse packet type
    int pktTypeInt = std::atoi(tokens[3].c_str());
    PktType packetType = static_cast<PktType>(pktTypeInt);
    
    // Parse optional hex data
    std::vector<uint8_t> data = CommandUtils::parseHexData(tokens, 4);
    
    // Get source MAC and send
    std::string srcMacStr = devices[srcDevice].peerCommsDriver->getMacString();
    uint8_t srcMac[6];
    CommandUtils::parseMacString(srcMacStr, srcMac);
    
    NativePeerBroker::getInstance().sendPacket(
        srcMac, dstMac, packetType,
        data.empty() ? nullptr : data.data(),
        data.size());
    
    std::string dstStr = isBroadcast ? "broadcast" : devices[dstDevice].deviceId;
    result.message = "Sent packet type " + std::to_string(pktTypeInt) + 
                     " from " + devices[srcDevice].deviceId + 
                     " to " + dstStr + 
                     " (" + std::to_string(data.size()) + " bytes)";
    return result;
}

CommandResult NetworkCommands::cmdInject(const std::vector<std::string>& tokens,
                                         const std::vector<DeviceInstance>& devices) {
    CommandResult result;
    
    if (tokens.size() < 3) {
        result.message = "Usage: inject <dst> <type> [hexdata] - inject from external source";
        return result;
    }
    
    int dstDevice = CommandUtils::findDevice(tokens[1], devices, -1);
    if (dstDevice < 0) {
        result.message = "Invalid destination device: " + tokens[1];
        return result;
    }
    
    int pktTypeInt = std::atoi(tokens[2].c_str());
    PktType packetType = static_cast<PktType>(pktTypeInt);
    
    std::vector<uint8_t> data = CommandUtils::parseHexData(tokens, 3);
    
    // Create a fake external MAC address
    uint8_t externalMac[6] = {0xEE, 0xEE, 0xEE, 0x00, 0x00, 0x01};
    
    // Get destination MAC
    std::string dstMacStr = devices[dstDevice].peerCommsDriver->getMacString();
    uint8_t dstMac[6];
    CommandUtils::parseMacString(dstMacStr, dstMac);
    
    NativePeerBroker::getInstance().sendPacket(
        externalMac, dstMac, packetType,
        data.empty() ? nullptr : data.data(),
        data.size());
    
    result.message = "Injected packet type " + std::to_string(pktTypeInt) + 
                     " to " + devices[dstDevice].deviceId +
                     " (" + std::to_string(data.size()) + " bytes)";
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
