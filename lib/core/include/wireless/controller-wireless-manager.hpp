#pragma once

#include <cstring>
#include <functional>
#include <map>

#include "device/drivers/button.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/wireless-manager.hpp"

class RemoteDeviceCoordinator;

struct ControllerPacket {
    int command;
    ButtonIdentifier buttonId;
    ButtonInteraction interactionId;
} __attribute__((packed));

enum ControllerCmd {
    INTERACTION_REQUEST = 0,
    CONTROLLER_CMD_COUNT,
    CONTROLLER_INVALID_CMD = 0xFF
};

struct GameSelectPacket {
    int command;
    int gameId;
} __attribute__((packed));

enum GameSelectCmd {
    GAME_SELECT = 0,
    GAME_SELECT_CMD_COUNT,
    GAME_SELECT_INVALID_CMD = 0xFF
};

enum GameSelectId {
    CONTROLLER_1 = 0,
    GAME_SELECT_ID_COUNT,
    GAME_SELECT_INVALID_ID = 0xFF
};

struct GameSelectCommand {
    uint8_t wifiMacAddr[6];
    bool wifiMacAddrValid;
    int command;
    GameSelectId gameId;

    GameSelectCommand() = delete;

    GameSelectCommand(const uint8_t* macAddress, int command, GameSelectId gameId)
        : wifiMacAddrValid(macAddress != nullptr)
        , command(command)
        , gameId(gameId) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }
};

struct GameResponsePacket {
    int responseId;
} __attribute__((packed));

enum GameResponseId {
    ACCEPTED = 0,
    REJECTED = 1,
    GAME_RESPONSE_ID_COUNT,
    GAME_RESPONSE_INVALID_ID = 0xFF
};

struct GameResponseCommand {
    uint8_t wifiMacAddr[6];
    bool wifiMacAddrValid;
    GameResponseId responseId;

    GameResponseCommand() = delete;

    GameResponseCommand(const uint8_t* macAddress, GameResponseId responseId)
        : wifiMacAddrValid(macAddress != nullptr)
        , responseId(responseId) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }
};

struct ControllerCommand {
    uint8_t wifiMacAddr[6];
    bool wifiMacAddrValid;
    int command;
    ButtonIdentifier buttonId;
    ButtonInteraction interactionId;

    ControllerCommand() = delete;

    ControllerCommand(const uint8_t* macAddress,
                      int command,
                      ButtonIdentifier buttonId,
                      ButtonInteraction interactionId)
        : wifiMacAddrValid(macAddress != nullptr)
        , command(command)
        , buttonId(buttonId)
        , interactionId(interactionId) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }
};

class ControllerWirelessManager {
public:
    ControllerWirelessManager();
    ~ControllerWirelessManager();

    void initialize(WirelessManager* wirelessManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);

    int processControllerCommand(const uint8_t* macAddress, const uint8_t* data, size_t dataLen);

    int processGameSelectCommand(const uint8_t* macAddress, const uint8_t* data, size_t dataLen);

    int processGameResponseCommand(const uint8_t* macAddress, const uint8_t* data, size_t dataLen);

    int sendPacket(int command,
                   ButtonIdentifier buttonId,
                   ButtonInteraction interactionId,
                   SerialIdentifier serialPort);

    int sendGameSelectPacket(GameSelectId gameId, SerialIdentifier serialPort);

    int sendGameResponsePacket(GameResponseId responseId, SerialIdentifier serialPort);

    void setMacPeer(const uint8_t* macAddress);

    void setPacketReceivedCallback(const std::function<void(const ControllerCommand&)>& callback,
                                   SerialIdentifier port);

    void setGameSelectReceivedCallback(const std::function<void(const GameSelectCommand&)>& callback,
                                       SerialIdentifier port);

    void setGameResponseReceivedCallback(const std::function<void(const GameResponseCommand&)>& callback,
                                         SerialIdentifier port);

    void clearCallback();

private:
    WirelessManager* wirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    uint8_t macPeer[6];

    std::map<SerialIdentifier, std::function<void(const ControllerCommand&)>> packetReceivedCallbacks;
    std::map<SerialIdentifier, std::function<void(const GameSelectCommand&)>> gameSelectReceivedCallbacks;
    std::map<SerialIdentifier, std::function<void(const GameResponseCommand&)>> gameResponseReceivedCallbacks;
};
