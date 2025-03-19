#pragma once

//
// Created by Elli Furedy on 1/21/2025.
//
#include <vector>
#include <queue>
#include <esp_now.h>
#include <cstring>  // For memcpy
#include <functional>
#include <map>
#include "simple-timer.hpp"
#include "player.hpp"
#include <ArduinoJson.h>

using namespace std;

enum QDCommand {
    // Serial Commands
    LETS_PLAY = 0,
    // Handshake Commands
    CONNECTION_CONFIRMED = 2, //Bounty -> Hunter
    HUNTER_RECEIVE_MATCH = 3, //Hunter -> Bounty
    BOUNTY_RECEIVE_OPPONENT_ID = 4, //Bounty -> Hunter
    HUNTER_RECEIVE_FINAL_ACK = 5, //Hunter -> Bounty
    BOUNTY_RECEIVE_FINAL_ACK = 6, //Bounty -> Hunter
    STARTING_LINE = 7, //BOTH
    // Game Commands
    HACK = 7,
    HACK_ACK = 8, 
    HACK_CONFIRMED = 9,
    LOCKDOWN = 10,
    LOCKDOWN_ACK = 11,
    LOCKDOWN_CONFIRMED = 12,    
    DRAW_RESULT = 13,
    COMMAND_COUNT,  // Always add new commands above this line
    INVALID_COMMAND = 0xFF
};

enum MessageSource {
    SOURCE_WIRELESS = 0,
    SOURCE_SERIAL = 1
};

// Define the SerialCommand structure
struct SerialCommand {
    int command;
    String data;
};

struct QuickdrawCommand {
    string wifiMacAddr;
    string matchId;
    string opponentId;
    int command;
    int ackCount;
    long drawTimeMs;
    MessageSource source;
    unsigned long timestamp;

    QuickdrawCommand() : command(0), ackCount(0), drawTimeMs(0), matchId(""), opponentId(""), 
                         wifiMacAddr(""), source(SOURCE_WIRELESS), timestamp(0) {}

    QuickdrawCommand(string macAddress, int command, long drawTimeMs, int ackCount, 
                    string matchId, string opponentId, MessageSource src = SOURCE_WIRELESS) :
        command(command), drawTimeMs(drawTimeMs), ackCount(ackCount), matchId(matchId), 
        opponentId(opponentId), wifiMacAddr(macAddress), source(src) {
            timestamp = millis();
        }
};

// Define expected command sequences for state validation
struct CommandSequence {
    vector<int> expectedCommands;
    int currentIndex;
    
    CommandSequence() : currentIndex(0) {}
    CommandSequence(const vector<int>& cmds) : expectedCommands(cmds), currentIndex(0) {}
    
    bool isNextCommand(int cmd) {
        if (currentIndex >= expectedCommands.size()) return false;
        return expectedCommands[currentIndex] == cmd;
    }
    
    void advance() {
        if (currentIndex < expectedCommands.size()) {
            currentIndex++;
        }
    }
    
    int getCurrentExpectedCommand() {
        if (currentIndex < expectedCommands.size()) {
            return expectedCommands[currentIndex];
        }
        return INVALID_COMMAND;
    }
    
    void reset() {
        currentIndex = 0;
    }
    
    bool isComplete() {
        return currentIndex >= expectedCommands.size();
    }
    
    int getIndex() {
        return currentIndex;
    }
};

typedef std::map<int, QuickdrawCommand> QDCommandTracker;

class QuickdrawCommsManager {
public:
    static QuickdrawCommsManager* GetInstance();

    void initialize(Player* player, long broadcastCooldown);

    // Process incoming wireless messages
    int processWirelessCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);
    
    // Process incoming serial messages
    int processSerialCommand(const string& jsonData);

    void setPacketReceivedCallback(std::function<void(QuickdrawCommand)> callback);

    int broadcastPacket(const string& macAddress, 
                       int command, 
                       int drawTimeMs = 0, 
                       int ackCount = 0, 
                       const string& matchId = "", 
                       const string& opponentId = "");
                       
    // Send serial messages (now gets PDN instance on demand)
    int sendSerialMessage(int command, const string& data);

    void clearCallbacks();

    int getPacketAckCount(int command);

    void clearPackets();

    void clearPacket(int command);

    void logPacket(QuickdrawCommand packet);
    
    // Message queue management
    void enqueueMessage(QuickdrawCommand cmd);
    QuickdrawCommand dequeueMessage();
    bool hasMessages();
    void clearMessageQueue();
    
    // Command sequence management
    void setCommandSequence(bool isBounty);
    bool validateNextCommand(int command);
    void syncStateToCommand(int command);
    int getExpectedNextCommand();
    void resetCommandSequence();

private:
    QuickdrawCommsManager();

    std::function<void(QuickdrawCommand)> packetReceivedCallback;

    Player* player;

    QDCommandTracker commandTracker;

    SimpleTimer broadcastTimer;

    long broadcastDelay;
    
    // Message queue for unified handling
    std::queue<QuickdrawCommand> messageQueue;
    
    // Command sequences for state validation
    CommandSequence bountySequence;
    CommandSequence hunterSequence;
    CommandSequence* activeSequence;
}; 