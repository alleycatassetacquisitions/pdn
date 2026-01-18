#pragma once

//
// Created by Elli Furedy on 1/21/2025.
//
#include <vector>
#include <cstring>  // For memcpy
#include <functional>
#include <map>
#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "id-generator.hpp"
#include "mac-functions.hpp"
#include "device/drivers/peer-comms-interface.hpp"

enum QDCommand {
    // Handshake Commands
    CONNECTION_CONFIRMED = 0, //Bounty -> Hunter
    HUNTER_RECEIVE_MATCH = 1, //Hunter -> Bounty
    BOUNTY_FINAL_ACK = 4, //Bounty -> Hunter
    
    // Game Commands
    HACK = 6,
    HACK_ACK = 7, 
    HACK_CONFIRMED = 8,
    LOCKDOWN = 9,
    LOCKDOWN_ACK = 10,
    LOCKDOWN_CONFIRMED = 11,    
    DRAW_RESULT = 12,
    NEVER_PRESSED = 13,
    COMMAND_COUNT,  // Always add new commands above this line
    INVALID_COMMAND = 0xFF

};

struct QuickdrawCommand {
    std::string wifiMacAddr;
    int command;
    Match match;

    QuickdrawCommand() : command(0), match(), wifiMacAddr("") {}

    QuickdrawCommand(std::string macAddress, int command, Match match) :
        command(command), match(match), wifiMacAddr(macAddress) {    }



};

typedef std::map<int, QuickdrawCommand> QDCommandTracker;

class QuickdrawWirelessManager {
public:
    static QuickdrawWirelessManager* GetInstance();

    void initialize(Player* player, PeerCommsInterface* peerComms, long broadcastCooldown);

    int processQuickdrawCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(std::function<void(QuickdrawCommand)> callback);

    int broadcastPacket(const std::string& macAddress, 
                       int command, 
                       Match match);

    void clearCallbacks();

    int getPacketAckCount(int command);

    void clearPackets();

    void clearPacket(int command);

    void logPacket(QuickdrawCommand packet);

private:
    QuickdrawWirelessManager();

    PeerCommsInterface* peerComms;

    std::function<void(QuickdrawCommand)> packetReceivedCallback;

    Player* player;

    QDCommandTracker commandTracker;

    SimpleTimer broadcastTimer;

    long broadcastDelay;
};