#pragma once

#include <cstring>

#include "device/drivers/serial-wrapper.hpp"
#include "device/wireless-manager.hpp"
#include "symbol-match/symbol.hpp"

struct SymbolMatchPacket {
    int command;
    SymbolId symbolId;
    /// Sender's serial jack toward the peer (SerialIdentifier as int for packing).
    int serialPort;
} __attribute__((packed));

enum SMCommand {
    SEND_SYMBOL = 0,
    SM_COMMAND_COUNT,  // Always add new commands above this line
    SM_INVALID_COMMAND = 0xFF
};


struct SymbolMatchCommand {
    uint8_t wifiMacAddr[6];
    bool wifiMacAddrValid;
    int command;
    SymbolId symbolId;
    SerialIdentifier serialPort;

    SymbolMatchCommand() = delete;

    SymbolMatchCommand(const uint8_t* macAddress, int command, SymbolId symbolId, SerialIdentifier serialPort)
        : wifiMacAddrValid(macAddress != nullptr), command(command), symbolId(symbolId), serialPort(serialPort) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }
};

class SymbolWirelessManager {
public:
    SymbolWirelessManager();
    ~SymbolWirelessManager();

    void initialize(WirelessManager* wirelessManager);

    int processSymbolMatchCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    int sendPacket(int command, SymbolId symbolId, SerialIdentifier serialPort);

    void setMacPeer(const uint8_t* macAddress);

    void setPacketReceivedCallback(const std::function<void(const SymbolMatchCommand&)>& callback);

    void clearCallback();

private:
    WirelessManager* wirelessManager;
    uint8_t macPeer[6];

    std::function<void(const SymbolMatchCommand&)> packetReceivedCallback;

};
