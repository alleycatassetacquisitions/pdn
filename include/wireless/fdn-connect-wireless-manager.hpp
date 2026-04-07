#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include "device/wireless-manager.hpp"
#include "device/drivers/button.hpp"
#include "fdn-constants.hpp"

constexpr int FDN_HACK_SEQUENCE_LENGTH = 8;

enum FdnConnectCmd {
    PDN_CONNECT = 0,        // PDN → FDN: announces connection, carries player ID
    SEND_HACK_SEQUENCE = 1, // FDN → PDN: transmits the 8-button sequence
    BUTTON_PRESS = 2,       // PDN → FDN: player pressed a button
    FDN_CONNECT_CMD_COUNT,
    FDN_CONNECT_INVALID_CMD = 0xFF
};

class FDNConnectWirelessManager {
public:
    FDNConnectWirelessManager();
    ~FDNConnectWirelessManager();

    void initialize(WirelessManager* wirelessManager);

    void setPeer(const uint8_t* macAddr);
    void clearPeer();

    int sendHackSequence(const ButtonIdentifier* sequence);

    void setConnectCallback(std::function<void(const std::string& playerId, const uint8_t* senderMac)> callback);
    void setButtonPressCallback(std::function<void(ButtonIdentifier btn)> callback);
    void clearCallbacks();

    int processPacket(const uint8_t* senderMac, const uint8_t* data, size_t dataLen);

private:
    WirelessManager* wirelessManager = nullptr;
    std::array<uint8_t, 6> peerMac{};
    bool hasPeer = false;

    std::function<void(const std::string&, const uint8_t*)> connectCallback;

    std::function<void(ButtonIdentifier)> buttonPressCallback;
};
