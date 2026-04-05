#pragma once

//
// Created by Elli Furedy on 1/21/2025.
//
#include <vector>
#include <cstring>  // For memcpy
#include <functional>
#include <map>
#include <cstdint>
#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "id-generator.hpp"
#include "mac-functions.hpp"
#include "device/wireless-manager.hpp"

// Wire format transmitted over ESP-NOW for every quickdraw command.
// Defined here so tests can construct and inspect packets without duplicating the layout.
struct QuickdrawPacket {
    char matchId[37];  // IdGenerator::UUID_BUFFER_SIZE
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;
    int  command;
} __attribute__((packed));

enum QDCommand {
    // Game Commands
    // HACK = 6,
    // HACK_ACK = 7, 
    // HACK_CONFIRMED = 8,
    // LOCKDOWN = 9,
    // LOCKDOWN_ACK = 10,
    // LOCKDOWN_CONFIRMED = 11,    
    SEND_MATCH_ID = 6,
    MATCH_ID_ACK = 7,
    MATCH_ROLE_MISMATCH = 8,
    DRAW_RESULT = 9,
    NEVER_PRESSED = 10,
    COMMAND_COUNT,  // Always add new commands above this line
    INVALID_COMMAND = 0xFF
};

struct QuickdrawCommand {
    const uint8_t* wifiMacAddr;
    int command;
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;

    QuickdrawCommand(const uint8_t* macAddress, int command, const char* matchId, const char* playerId, long playerDrawTime, bool isHunter)
        : command(command), playerDrawTime(playerDrawTime), isHunter(isHunter) {
        this->wifiMacAddr = macAddress;

        memcpy(this->matchId, matchId, IdGenerator::UUID_BUFFER_SIZE);

        memcpy(this->playerId, playerId, 5);
    }
};

using QDCommandTracker = std::map<int, QuickdrawCommand>;

class QuickdrawWirelessManager {
public:
    QuickdrawWirelessManager();
    virtual ~QuickdrawWirelessManager();

    void initialize(Player* player, WirelessManager* wirelessManager);

    int processQuickdrawCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(const std::function<void(const QuickdrawCommand&)>& callback);

    virtual int broadcastPacket(const uint8_t* macAddress,
                               QuickdrawCommand& command);

    void clearCallbacks();

private:
    WirelessManager* wirelessManager;

    std::function<void(const QuickdrawCommand&)> packetReceivedCallback;

    Player* player;
};
