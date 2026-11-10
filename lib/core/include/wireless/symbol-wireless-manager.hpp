#pragma once

#include <functional>
#include <map>

#include "device/drivers/serial-wrapper.hpp"
#include "device/wireless-manager.hpp"
#include "symbol.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

class RemoteDeviceCoordinator;

struct SymbolMatchPacket {
    uint8_t seqId;
    int command;
    SymbolId symbolId;
} __attribute__((packed));

namespace SMCommand {
constexpr int SEND_SYMBOL = 0;
constexpr int SYMBOL_MATCH_SUCCESS = 1;
constexpr int SYMBOLS_REFRESHED = 2;
}

struct SymbolMatchCommand {
    int command;
    SymbolId symbolId;
};

class SymbolWirelessManager {
public:
    /** Claims the kSymbolMatchCommand channel on wirelessManager. */
    SymbolWirelessManager(WirelessManager* wirelessManager,
                          RemoteDeviceCoordinator* remoteDeviceCoordinator);

    /** Sends command to macPeer (jack is used for logging only). */
    void sendPacket(int command, SymbolId symbolId, SerialIdentifier jack);

    /** Sets the address every subsequent sendPacket goes to. */
    void setMacPeer(const uint8_t* macAddress);

    /** Registers the callback for commands arriving on `jack`. One slot per jack. */
    void setPacketReceivedCallback(const std::function<void(const SymbolMatchCommand&)>& callback,
                                   SerialIdentifier jack);

    /** Drops every registered jack callback and cancels pending sends. */
    void clearCallback();

private:
    void onSymbolPacket(const uint8_t* macAddress, const SymbolMatchPacket& packet);

    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    uint8_t macPeer[6];
    ReliableChannel<SymbolMatchPacket> channel;

    std::map<SerialIdentifier, std::function<void(const SymbolMatchCommand&)>> packetReceivedCallbacks;
};
