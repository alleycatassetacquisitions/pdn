#pragma once

//
// Created by Elli Furedy on 2/22/2025.
//
#include <cstring>
#include <functional>
#include <map>
#include "game/player.hpp"
#include "mac-functions.hpp"
#include "device/wireless-manager.hpp"
#include "device/serial-manager.hpp"

enum HSCommand {
    EXCHANGE_ID = 0,
    NOTIFY_DISCONNECT = 1,
    HS_COMMAND_COUNT,   // Always add new commands above this line
    HS_INVALID_COMMAND = 0xFF
};

struct HandshakeCommand {
    std::string wifiMacAddr;
    int command;
    SerialIdentifier jack;

    HandshakeCommand() = delete;

    HandshakeCommand(const std::string& macAddress, int command, SerialIdentifier jack) :
        command(command), wifiMacAddr(macAddress), jack(jack) {}
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

    void setMacPeer(const std::string& macAddress, SerialIdentifier jack);

    void removeMacPeer(SerialIdentifier jack);

private:
    WirelessManager* wirelessManager;

    std::map<SerialIdentifier, std::function<void(HandshakeCommand)>> callbacks;

    std::map<SerialIdentifier, std::string> macPeers;
};
