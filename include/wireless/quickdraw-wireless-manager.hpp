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
    uint8_t wifiMacAddr[6];
    bool wifiMacAddrValid;
    int command;
    Match match;

    QuickdrawCommand() : wifiMacAddrValid(false), command(0), match() {
        memset(wifiMacAddr, 0, 6);
    }

    QuickdrawCommand(const uint8_t* macAddress, int command, const Match& match)
        : wifiMacAddrValid(macAddress != nullptr), command(command), match(match) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }

    /**
     * Hot-path constructor. Constructs Match in-place from raw packet fields,
     * avoiding an intermediate Match object and the resulting copy.
     * Preconditions mirror Match's 5-arg constructor (full-length IDs required).
     */
    QuickdrawCommand(const uint8_t* macAddress, int cmd,
                     const char* matchId, const char* hunterId, const char* bountyId,
                     unsigned long hunterTime, unsigned long bountyTime)
        : wifiMacAddrValid(macAddress != nullptr), command(cmd),
          match(matchId, hunterId, bountyId, hunterTime, bountyTime) {
        if (macAddress) {
            memcpy(wifiMacAddr, macAddress, 6);
        } else {
            memset(wifiMacAddr, 0, 6);
        }
    }
};

using QDCommandTracker = std::map<int, QuickdrawCommand>;

class QuickdrawWirelessManager {
public:
    QuickdrawWirelessManager();
    ~QuickdrawWirelessManager();

    void initialize(Player* player, WirelessManager* wirelessManager, long broadcastCooldown);

    int processQuickdrawCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(const std::function<void(const QuickdrawCommand&)>& callback);

    int broadcastPacket(const uint8_t* macAddress,
                       int command,
                       const Match& match);

    void clearCallbacks();

private:
    WirelessManager* wirelessManager;

    std::function<void(const QuickdrawCommand&)> packetReceivedCallback;

    Player* player;

    QDCommandTracker commandTracker;

    SimpleTimer broadcastTimer;

    long broadcastDelay;
};
