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
    // Handshake Commands
    CONNECTION_CONFIRMED = 0, //Bounty -> Hunter
    HUNTER_RECEIVE_MATCH = 1, //Hunter -> Bounty
    BOUNTY_RECEIVE_OPPONENT_ID = 2, //Bounty -> Hunter
    HUNTER_RECEIVE_FINAL_ACK = 3, //Hunter -> Bounty
    BOUNTY_RECEIVE_FINAL_ACK = 4, //Bounty -> Hunter
    STARTING_LINE = 5, //BOTH
    // Game Commands

    HACK = 6,
    HACK_ACK = 7, 
    HACK_CONFIRMED = 8,
    LOCKDOWN = 9,
    LOCKDOWN_ACK = 10,
    LOCKDOWN_CONFIRMED = 11,    
    DRAW_RESULT = 12,
    COMMAND_COUNT,  // Always add new commands above this line
    INVALID_COMMAND = 0xFF

};

struct QuickdrawCommand {
    string wifiMacAddr;
    string matchId;
    string opponentId;
    int command;
    int ackCount;
    long drawTimeMs;

    QuickdrawCommand() : command(0), ackCount(0), drawTimeMs(0), matchId(""), opponentId(""), wifiMacAddr("") {}

    QuickdrawCommand(string macAddress, int command, long drawTimeMs, int ackCount, string matchId, string opponentId) :
        command(command), drawTimeMs(drawTimeMs), ackCount(ackCount), matchId(matchId), opponentId(opponentId), wifiMacAddr(macAddress) {    }



};

typedef std::map<int, QuickdrawCommand> QDCommandTracker;

class QuickdrawWirelessManager {
public:
    static QuickdrawWirelessManager* GetInstance();

    void initialize(Player* player, long broadcastCooldown);

    int processQuickdrawCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(std::function<void(QuickdrawCommand)> callback);

    int broadcastPacket(const string& macAddress, 
                       int command, 
                       int drawTimeMs = 0, 
                       int ackCount = 0, 
                       const string& matchId = "", 
                       const string& opponentId = "");

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