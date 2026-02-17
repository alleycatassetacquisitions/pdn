#ifdef NATIVE_BUILD

#include "cli/commands/command-utils.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace cli {

int CommandUtils::findDevice(const std::string& target,
                              const std::vector<DeviceInstance>& devices,
                              int defaultDevice) {
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i].deviceId == target || std::to_string(i) == target) {
            return static_cast<int>(i);
        }
    }
    return defaultDevice;
}

std::vector<std::string> CommandUtils::tokenize(const std::string& cmd) {
    std::vector<std::string> tokens;
    std::string token;
    for (char c : cmd) {
        if (c == ' ') {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

void CommandUtils::parseMacString(const std::string& macStr, uint8_t* macOut) {
    unsigned int bytes[6];
    sscanf(macStr.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
           &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]);
    for (int i = 0; i < 6; i++) {
        macOut[i] = static_cast<uint8_t>(bytes[i]);
    }
}

std::vector<uint8_t> CommandUtils::parseHexData(const std::vector<std::string>& tokens, size_t startIndex) {
    std::vector<uint8_t> data;
    for (size_t i = startIndex; i < tokens.size(); i++) {
        unsigned int byte;
        if (tokens[i].find("0x") == 0 || tokens[i].find("0X") == 0) {
            byte = std::strtoul(tokens[i].c_str() + 2, nullptr, 16);
        } else {
            byte = std::strtoul(tokens[i].c_str(), nullptr, 16);
        }
        data.push_back(static_cast<uint8_t>(byte));
    }
    return data;
}

} // namespace cli

#endif // NATIVE_BUILD
