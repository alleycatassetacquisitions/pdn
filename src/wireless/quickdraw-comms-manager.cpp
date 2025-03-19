//
// Created by Elli Furedy on 1/24/2025.
//
#include "wireless/quickdraw-comms-manager.hpp"
#include "device/pdn.hpp"  // For getting PDN instance
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include "wireless/esp-now-comms.hpp"

#define UUID_LEN 32

struct QuickdrawPacket {
    char id[UUID_LEN];
    char matchId[UUID_LEN];
    char opponentId[UUID_LEN];
    int command;
    long resultTime;
    int ackCount;
} __attribute__((packed));

QuickdrawCommsManager* QuickdrawCommsManager::GetInstance() {
    static QuickdrawCommsManager instance;
    return &instance;
}

QuickdrawCommsManager::QuickdrawCommsManager() : broadcastTimer() {
    // Initialize command sequences for both device types
    vector<int> bountyCommands = {
        CONNECTION_CONFIRMED,
        BOUNTY_RECEIVE_OPPONENT_ID,
        BOUNTY_RECEIVE_FINAL_ACK,
        STARTING_LINE,
        // Game commands
        HACK,
        HACK_ACK,
        HACK_CONFIRMED,
        DRAW_RESULT
    };
    
    vector<int> hunterCommands = {
        HUNTER_RECEIVE_MATCH,
        HUNTER_RECEIVE_FINAL_ACK,
        STARTING_LINE,
        // Game commands
        LOCKDOWN,
        LOCKDOWN_ACK,
        LOCKDOWN_CONFIRMED,
        DRAW_RESULT
    };
    
    bountySequence = CommandSequence(bountyCommands);
    hunterSequence = CommandSequence(hunterCommands);
    activeSequence = nullptr; // Will be set during initialization
}

void QuickdrawCommsManager::initialize(Player* player, long broadcastDelay) {
    this->player = player;
    this->broadcastDelay = broadcastDelay;
    
    // Set the appropriate command sequence based on player role
    setCommandSequence(player->isHunter());
    
    // No longer register for serial callbacks here
    // This will be done in main.cpp
}

void QuickdrawCommsManager::setCommandSequence(bool isHunter) {
    ESP_LOGI("QCM", "Setting command sequence for %s", isHunter ? "Hunter" : "Bounty");
    if (isHunter) {
        activeSequence = &hunterSequence;
    } else {
        activeSequence = &bountySequence;
    }
    activeSequence->reset();
}

bool QuickdrawCommsManager::validateNextCommand(int command) {
    if (!activeSequence) {
        ESP_LOGE("QCM", "No active sequence set");
        return false;
    }
    
    bool isValid = activeSequence->isNextCommand(command);
    if (isValid) {
        ESP_LOGI("QCM", "Command %d is valid as next command (index %d)", 
                command, activeSequence->getIndex());
        activeSequence->advance();
    } else {
        ESP_LOGW("QCM", "Command %d is NOT valid as next command. Expected %d (index %d)", 
                command, activeSequence->getCurrentExpectedCommand(), activeSequence->getIndex());
    }
    
    return isValid;
}

//Sync State should exist in the state machine. We'll have to revisit the purpose of this function.
void QuickdrawCommsManager::syncStateToCommand(int command) {
    if (!activeSequence) {
        ESP_LOGE("QCM", "No active sequence set");
        return;
    }
    
    // Find the command in the sequence and advance to that point
    for (size_t i = 0; i < activeSequence->expectedCommands.size(); i++) {
        if (activeSequence->expectedCommands[i] == command) {
            //need to ensure we don't have an index out of bounds error here.
            //if we are at the last command, we stay there until we get a signal from
            //the state machine to reset the sequence.
            if(i == activeSequence->expectedCommands.size() - 1) {
                ESP_LOGI("QCM", "Synchronized state to command %d (index %d)", 
                        command, activeSequence->getIndex() - 1);
                return;
            } else {
                activeSequence->currentIndex = i + 1; // Set index to just after the found command
            }
            ESP_LOGI("QCM", "Synchronized state to command %d (index %d)", 
                    command, activeSequence->getIndex() - 1);
            return;
        }
    }
    
    ESP_LOGW("QCM", "Command %d not found in active sequence", command);
}

int QuickdrawCommsManager::getExpectedNextCommand() {
    if (!activeSequence) {
        ESP_LOGE("QCM", "No active sequence set");
        return INVALID_COMMAND;
    }
    //This seems incorrect - the met
    int cmd = activeSequence->getCurrentExpectedCommand();
    ESP_LOGI("QCM", "Next expected command: %d (index %d)", cmd, activeSequence->getIndex());
    return cmd;
}

void QuickdrawCommsManager::resetCommandSequence() {
    if (activeSequence) {
        ESP_LOGI("QCM", "Resetting command sequence");
        activeSequence->reset();
    } else {
        ESP_LOGW("QCM", "No active sequence to reset");
    }
}

void QuickdrawCommsManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
}

void QuickdrawCommsManager::setPacketReceivedCallback(std::function<void(QuickdrawCommand)> callback) {
    packetReceivedCallback = callback;
}

//We don't need a broadcast timer for this class. We are never sending repeated commands.
int QuickdrawCommsManager::broadcastPacket(const string& macAddress, 
                                          int command, 
                                          int drawTimeMs, 
                                          int ackCount,
                                          const string& matchId, 
                                          const string& opponentId) {
    if(!broadcastTimer.isRunning() || broadcastTimer.expired()) {
        QuickdrawPacket qdPacket;
        
        // Safely copy the player ID
        strncpy(qdPacket.id, player->getUserID().c_str(), UUID_LEN);
        ESP_LOGI("QCM", "Player ID: %s", qdPacket.id);
        
        // Safely copy the match ID
        strncpy(qdPacket.matchId, matchId.c_str(), UUID_LEN);
        qdPacket.matchId[UUID_LEN] = '\0';
        ESP_LOGI("QCM", "Match ID: %s", qdPacket.matchId);
        
        // Safely copy the opponent ID
        strncpy(qdPacket.opponentId, opponentId.c_str(), UUID_LEN);
        qdPacket.opponentId[UUID_LEN] = '\0';
        ESP_LOGI("QCM", "Opponent ID: %s", qdPacket.opponentId);
        
        qdPacket.command = command;
        qdPacket.resultTime = drawTimeMs;
        qdPacket.ackCount = ackCount;

        ESP_LOGI("QCM", "Broadcasting command %i", command);

        int ret = EspNowManager::GetInstance()->SendData(
            ESP_NOW_BROADCAST_ADDR,
            PktType::kQuickdrawCommand,
            (uint8_t*)&qdPacket,
            sizeof(qdPacket));

        broadcastTimer.setTimer(broadcastDelay);
        return ret;
    }
    return -1;
}

//Serial commands are not json. We can assume they are just basic strings.
//We should assume this is the callback function we register with the PDN's device sesrial.
int QuickdrawCommsManager::processSerialCommand(const string& jsonData) {
    // Parse JSON data from serial
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        ESP_LOGE("QCM", "Failed to parse JSON: %s", error.c_str());
        return INVALID_COMMAND;
    }
    
    QuickdrawCommand cmd;
    cmd.source = SOURCE_SERIAL;
    cmd.timestamp = millis();
    
    // Extract fields from JSON
    cmd.command = doc["command"];
    cmd.drawTimeMs = 0;
    cmd.ackCount = 0;
    cmd.matchId = "";
    cmd.opponentId = "";


    if(cmd.command == LETS_PLAY) {
        cmd.wifiMacAddr = doc["data"] | "";
    }
    
    ESP_LOGI("QCM", "Processed serial command: %d", cmd.command);
    
    // Add to message queue
    enqueueMessage(cmd);
    
    // Optionally trigger callback
    if (packetReceivedCallback) {
        packetReceivedCallback(cmd);
    }
    
    return cmd.command;
}

int QuickdrawCommsManager::sendSerialMessage(int command, const string& data) {
    // Get PDN instance on demand
    Device* pdn = PDN::GetInstance();

    // Create a JSON object for the serial command
    StaticJsonDocument<256> doc;
    
    // Create and populate the SerialCommand
    SerialCommand serialCmd;
    serialCmd.command = command;
    serialCmd.data = data.c_str();
    
    // Serialize the command to JSON
    doc["command"] = serialCmd.command;
    doc["data"] = serialCmd.data;
    
    // Convert to string
    String output;
    serializeJson(doc, output);
    
    if (!pdn) {
        ESP_LOGE("QCM", "Cannot send serial message: PDN instance not available");
        return INVALID_COMMAND;
    }
    
    pdn->writeString(output.c_str());
    ESP_LOGI("QCM", "Sent serial message: %s", output.c_str());
    
    return command;
}

//should be called processWirelessCommand
int QuickdrawCommsManager::processWirelessCommand(const uint8_t *macAddress, const uint8_t *data,
    const size_t dataLen) {

    if(dataLen != sizeof(QuickdrawPacket)) {
        ESP_LOGE("QCM", "Unexpected packet len for QuickdrawPacket. Got %lu but expected %lu\n",
                      dataLen, sizeof(QuickdrawPacket));

        return -1;
    }

    QuickdrawPacket *packet = (QuickdrawPacket *)data;

    QuickdrawCommand command = QuickdrawCommand(
            MacToString(macAddress),
            packet->command,
            packet->resultTime,
            packet->ackCount,
            packet->matchId,
            packet->opponentId,
            SOURCE_WIRELESS);  // Explicitly mark as wireless source

    // Add to message queue
    enqueueMessage(command);
    
    logPacket(command);
    if(packetReceivedCallback) {
        packetReceivedCallback(command);
    }

    return 1;
}


void QuickdrawCommsManager::enqueueMessage(QuickdrawCommand cmd) {
    messageQueue.push(cmd);
    ESP_LOGI("QCM", "Enqueued message, queue size: %d", messageQueue.size());
}

QuickdrawCommand QuickdrawCommsManager::dequeueMessage() {
    if (messageQueue.empty()) {
        ESP_LOGW("QCM", "Attempted to dequeue from empty message queue");
        return QuickdrawCommand(); // Return empty command
    }
    
    QuickdrawCommand cmd = messageQueue.front();
    messageQueue.pop();
    ESP_LOGI("QCM", "Dequeued message, remaining queue size: %d", messageQueue.size());
    return cmd;
}

bool QuickdrawCommsManager::hasMessages() {
    return !messageQueue.empty();
}

void QuickdrawCommsManager::clearMessageQueue() {
    int size = messageQueue.size();
    while (!messageQueue.empty()) {
        messageQueue.pop();
    }
    ESP_LOGI("QCM", "Cleared message queue of %d messages", size);
}

void QuickdrawCommsManager::clearPackets() {
    commandTracker.clear();
}

void QuickdrawCommsManager::clearPacket(int command) {
    commandTracker.erase(command);
}

int QuickdrawCommsManager::getPacketAckCount(int command) {
    if(commandTracker.find(command) == commandTracker.end()) {
        //No command found
        return 0;
    }

    return commandTracker[command].ackCount;
}

void QuickdrawCommsManager::logPacket(QuickdrawCommand packet) {
    // if(commandTracker.find(packet.command) == commandTracker.end()) {
        //No command found, place it in the tracker.
        commandTracker[packet.command] = packet;
    // } else {
    //     if(std::memcmp(commandTracker[packet.command].wifiMacAddr, packet.wifiMacAddr, 6) == 0) {
    //         //command came from the same user as the last one
    //         commandTracker[packet.command]
    //     }
    // }
} 