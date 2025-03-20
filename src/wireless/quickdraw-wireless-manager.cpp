//
// Created by Elli Furedy on 1/24/2025.
//
#include "wireless/quickdraw-wireless-manager.hpp"

#include <Arduino.h>

#include "wireless/esp-now-comms.hpp"
#include "id-generator.hpp"


struct QuickdrawPacket {
    char id[IdGenerator::UUID_BUFFER_SIZE];
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char opponentId[IdGenerator::UUID_BUFFER_SIZE];
    int command;
    long resultTime;
    int ackCount;


} __attribute__((packed));

QuickdrawWirelessManager *QuickdrawWirelessManager::GetInstance() {
    static QuickdrawWirelessManager instance;
    return &instance;
}

QuickdrawWirelessManager::QuickdrawWirelessManager() : broadcastTimer() {}

void QuickdrawWirelessManager::initialize(Player *player, long broadcastDelay) {
    this->player = player;
    this->broadcastDelay = broadcastDelay;
}

void QuickdrawWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
}

void QuickdrawWirelessManager::setPacketReceivedCallback(std::function<void (QuickdrawCommand)> callback) {
    packetReceivedCallback = callback;
}

int QuickdrawWirelessManager::broadcastPacket(const string& macAddress, 
                                             int command, 
                                             int drawTimeMs, 
                                             int ackCount,
                                             const string& matchId, 
                                             const string& opponentId) {
    QuickdrawPacket qdPacket;
    
    // Safely copy the player ID
    strncpy(qdPacket.id, player->getUserID().c_str(),  IdGenerator::UUID_STRING_LENGTH);
    qdPacket.id[IdGenerator::UUID_STRING_LENGTH] = '\0';  // Ensure null-termination
    ESP_LOGI("QWM", "Player ID: %s", qdPacket.id);
    
    // Safely copy the match ID
    strncpy(qdPacket.matchId, matchId.c_str(), IdGenerator::UUID_STRING_LENGTH);
    qdPacket.matchId[IdGenerator::UUID_STRING_LENGTH] = '\0';  // Ensure null-termination
    ESP_LOGI("QWM", "Match ID: %s", qdPacket.matchId);
    
    // Safely copy the opponent ID
    strncpy(qdPacket.opponentId, opponentId.c_str(), IdGenerator::UUID_STRING_LENGTH);
    qdPacket.opponentId[IdGenerator::UUID_STRING_LENGTH] = '\0';  // Ensure null-termination
    ESP_LOGI("QWM", "Opponent ID: %s", qdPacket.opponentId);
    
    qdPacket.command = command;
    qdPacket.resultTime = drawTimeMs;
    qdPacket.ackCount = ackCount;

    ESP_LOGI("QWM", "Broadcasting command %i", command);

    int ret = EspNowManager::GetInstance()->SendData(
        ESP_NOW_BROADCAST_ADDR,
        PktType::kQuickdrawCommand,
        (uint8_t*)&qdPacket,
        sizeof(qdPacket));

    return ret;
}


int QuickdrawWirelessManager::processQuickdrawCommand(const uint8_t *macAddress, const uint8_t *data,
    const size_t dataLen) {

    if(dataLen != sizeof(QuickdrawPacket)) {
        ESP_LOGE("RPM", "Unexpected packet len for PlayerInfoPkt. Got %lu but expected %lu\n",
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
            packet->opponentId);


    logPacket(command);
    if(packetReceivedCallback) {
        packetReceivedCallback(command);
    }

    return 1;
}

void QuickdrawWirelessManager::clearPackets() {
    commandTracker.clear();
}

void QuickdrawWirelessManager::clearPacket(int command) {
    commandTracker.erase(command);
}


int QuickdrawWirelessManager::getPacketAckCount(int command) {
    if(commandTracker.find(command) == commandTracker.end()) {
        //No command found
        return 0;
    }

    return commandTracker[command].ackCount;
}


void QuickdrawWirelessManager::logPacket(QuickdrawCommand packet) {
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

