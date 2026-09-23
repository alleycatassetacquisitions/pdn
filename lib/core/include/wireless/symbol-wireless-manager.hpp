#pragma once

#include <cstring>
#include <functional>
#include <map>

#include "device/drivers/serial-wrapper.hpp"
#include "device/wireless-manager.hpp"
#include "symbol.hpp"
#include "wireless/reliable-transport.hpp"

class RemoteDeviceCoordinator;

struct SymbolMatchPacket {
    uint8_t seqId;
    int command;
    SymbolId symbolId;
} __attribute__((packed));

/// Wire values for SymbolMatchPacket::command.
namespace SMCommand {
constexpr int SEND_SYMBOL = 0;
constexpr int SYMBOL_MATCH_SUCCESS = 1;
constexpr int SYMBOLS_REFRESHED = 2;
}  // namespace SMCommand

struct SymbolMatchCommand {
    uint8_t wifiMacAddr[6];
    bool wifiMacAddrValid;
    int command;
    SymbolId symbolId;

    /// No default: a command without a sender has nothing to answer.
    SymbolMatchCommand() = delete;

    /// Decoded form of one symbol frame; a null `macAddress` zeroes the address
    /// and clears wifiMacAddrValid.
    SymbolMatchCommand(const uint8_t* macAddress, int command, SymbolId symbolId)
        : wifiMacAddrValid(macAddress != nullptr)
        , command(command)
        , symbolId(symbolId) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }
};

class SymbolWirelessManager {
public:
    /// Leaves the manager inert until initialize() claims a channel.
    SymbolWirelessManager();
    /// Drops the channel, which clears the driver handlers it installed.
    ~SymbolWirelessManager();

    /// Claims the symbol channel, which is what makes the receive path live.
    void initialize(WirelessManager* wirelessManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);

    /// Drives the channel's retransmits. Called every loop tick.
    void sync();

    /// Sends one command to the peer set by setMacPeer, retried until the radio
    /// reports it landed. `serialPort` names the jack it is meant for; the
    /// address decides where it goes.
    void sendPacket(int command, SymbolId symbolId, SerialIdentifier serialPort);

    /// Sets the address every subsequent sendPacket goes to.
    void setMacPeer(const uint8_t* macAddress);

    /// Registers the callback for commands arriving on `port`. One slot per jack.
    void setPacketReceivedCallback(const std::function<void(const SymbolMatchCommand&)>& callback, SerialIdentifier port);

    /// Drops every registered port callback.
    void clearCallback();

private:
    // Resolves which jack the sender sits on and hands the command to that
    // port's callback.
    void onSymbolPacket(const uint8_t* macAddress, const SymbolMatchPacket& packet);

    WirelessManager* wirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    uint8_t macPeer[6];
    ReliableTransport* transport = nullptr;
    ReliableChannel<SymbolMatchPacket>* channel = nullptr;

    std::map<SerialIdentifier, std::function<void(const SymbolMatchCommand&)>> packetReceivedCallbacks;
};
