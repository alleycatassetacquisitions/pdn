#pragma once

//
// Created by Elli Furedy on 2/22/2025.
//
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include "device/drivers/serial-wrapper.hpp"
#include "game/player.hpp"
#include "mac-functions.hpp"
#include "device/wireless-manager.hpp"
#include "device/serial-manager.hpp"
#include "device/device-type.hpp"

enum HSCommand {
    EXCHANGE_ID = 0,
    NOTIFY_DISCONNECT = 1,
    HS_COMMAND_COUNT,   // Always add new commands above this line
    HS_INVALID_COMMAND = 0xFF
};

struct Peer {
    std::array<uint8_t, 6> macAddr;
    SerialIdentifier sid;
    DeviceType deviceType;
};

struct HandshakeCommand {
    uint8_t wifiMacAddr[6];
    bool wifiMacAddrValid;
    int deviceType;
    int command;
    SerialIdentifier sendingJack;
    SerialIdentifier receivingJack;

    HandshakeCommand() = delete;

    HandshakeCommand(const uint8_t* macAddress, int command, int deviceType, SerialIdentifier sendingJack, SerialIdentifier receivingJack)
        : wifiMacAddrValid(macAddress != nullptr), deviceType(deviceType), command(command), sendingJack(sendingJack), receivingJack(receivingJack) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }
};

class HandshakeWirelessManager {
public:
    HandshakeWirelessManager();
    ~HandshakeWirelessManager();

    void initialize(WirelessManager* wirelessManager);

    int processHandshakeCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(const std::function<void(HandshakeCommand)>& callback, SerialIdentifier jack);

    int sendPacket(int command, SerialIdentifier jack);

    void clearCallback(SerialIdentifier jack);

    void clearCallbacks();

    // Registers a direct peer on a jack. Returns false if the peer's MAC
    // equals our own (self-loopback or spoofing) — the peer is not stored
    // and the caller must not proceed with the handshake.
    bool setMacPeer(SerialIdentifier jack, Peer peer);

    void removeMacPeer(SerialIdentifier jack);

    const Peer* getMacPeer(SerialIdentifier jack) const;

private:
    WirelessManager* wirelessManager;

    std::map<SerialIdentifier, std::function<void(HandshakeCommand)>> callbacks;

    std::map<SerialIdentifier, Peer> macPeers;
};
