#pragma once

//
// Created by Elli Furedy on 1/21/2025.
//
#include <vector>
#include <esp_now.h>
#include <cstring>  // For memcpy
#include <functional>
#include <map>
#include "simple-timer.hpp"
#include "player.hpp"

using namespace std;

enum QDCommand {
    CONNECTION_CONFIRMED = 0,
    HACK = 1,
    HACK_ACK = 2,
    HACK_CONFIRMED = 3,
    LOCKDOWN = 4,
    LOCKDOWN_ACK = 5,
    LOCKDOWN_CONFIRMED = 6,
    DRAW_RESULT = 7,
    COMMAND_COUNT,  // Always add new commands above this line
    INVALID_COMMAND = 0xFF
};

struct QuickdrawCommand {
    uint8_t wifiMacAddr[ESP_NOW_ETH_ALEN];
    int command;
    int ackCount;
    long drawTimeMs;

    QuickdrawCommand() : command(0), ackCount(0), drawTimeMs(0) {
        memset(wifiMacAddr, 0, ESP_NOW_ETH_ALEN);
    }

    QuickdrawCommand(const uint8_t* macAddress, int command, long drawTimeMs, int ackCount) :
        command(command), drawTimeMs(drawTimeMs), ackCount(ackCount) {
        memcpy(wifiMacAddr, macAddress, ESP_NOW_ETH_ALEN);
    }
};

typedef std::map<int, QuickdrawCommand> QDCommandTracker;

class QuickdrawWirelessManager {
public:
    static QuickdrawWirelessManager* GetInstance();

    void initialize(Player* player, long broadcastCooldown);

    int processQuickdrawCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(std::function<void(QuickdrawCommand)> callback);

    int broadcastPacket(int command, int drawTimeMs = 0, int ackCount = 0);

    void clearCallbacks();

    int getPacketAckCount(int command);

    void clearPackets();

    void clearPacket(int command);

    void logPacket(QuickdrawCommand packet);

private:
    QuickdrawWirelessManager();

    std::function<void(QuickdrawCommand)> packetReceivedCallback;

    Player* player;

    QDCommandTracker commandTracker;

    SimpleTimer broadcastTimer;

    long broadcastDelay;
};